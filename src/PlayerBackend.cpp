#include "PlayerBackend.h"

#include "CoverExtractor.h"
#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "ScanSession.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

namespace {
const QString kDefaultTitle  = QStringLiteral("N/A");
const QString kDefaultArtist = QStringLiteral("Unknown Artist");
const QString kDefaultAlbum  = QStringLiteral("Unknown Album");

QString sortColumnName(int col) {
    switch (col) {
    case TrackModel::TitleColumn:    return QStringLiteral("title");
    case TrackModel::ArtistColumn:   return QStringLiteral("artist");
    case TrackModel::AlbumColumn:    return QStringLiteral("album");
    case TrackModel::DurationColumn: return QStringLiteral("duration");
    case TrackModel::TrackNoColumn:  return QStringLiteral("track_no");
    default:                         return {};
    }
}

QString sqlQuote(const QString &raw) {
    QString out = raw;
    out.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

QString sqlLikeEscape(const QString &raw) {
    QString out = raw;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('%'),  QLatin1String("\\%"));
    out.replace(QLatin1Char('_'),  QLatin1String("\\_"));
    out.replace(QLatin1Char('\''), QLatin1String("''"));
    return out;
}
}

PlayerBackend::PlayerBackend(QObject *parent)
    : QObject(parent),
      m_currentTitle(kDefaultTitle),
      m_currentArtist(kDefaultArtist),
      m_currentAlbum(kDefaultAlbum) {

    initDatabase();

    m_trackModel = new TrackModel(this);
    m_trackModel->setTable(QStringLiteral("tracks"));
    m_trackModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_trackModel->select();

    m_albumModel = new AlbumModel(this);
    refreshAlbumModel();

    m_artistModel = new ArtistModel(this);
    refreshArtistModel();

    m_queueModel = new QueueModel(&m_queue, this);

    m_libraryWatcher = new LibraryWatcher(this);
    connect(m_libraryWatcher, &LibraryWatcher::libraryChanged, this,
            &PlayerBackend::refreshAllModels);

    m_scanRefreshTimer = new QTimer(this);
    m_scanRefreshTimer->setSingleShot(true);
    m_scanRefreshTimer->setInterval(750);
    connect(m_scanRefreshTimer, &QTimer::timeout, this,
            &PlayerBackend::refreshAllModels);

    m_player      = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    // Volume/mute is now controlled at the PipeWire stream level (the per-app
    // slider in pavucontrol/wireplumber). Keep QAudioOutput's internal gain at
    // unity so the only attenuation comes from the system mixer.
    m_audioOutput->setVolume(1.0);
    m_audioOutput->setMuted(false);

    m_pwVolume = new PwVolumeController(this);
    connect(m_pwVolume, &PwVolumeController::volumeChanged,
            this, &PlayerBackend::volumeChanged);
    connect(m_pwVolume, &PwVolumeController::mutedChanged,
            this, &PlayerBackend::mutedChanged);

    setupMpris();

    connect(m_player, &QMediaPlayer::positionChanged,      this, &PlayerBackend::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,      this, &PlayerBackend::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PlayerBackend::playbackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged,   this,
            [this](QMediaPlayer::MediaStatus status) {
                switch (status) {
                case QMediaPlayer::LoadedMedia:
                case QMediaPlayer::BufferedMedia:
                    m_consecutiveInvalid = 0;
                    break;
                case QMediaPlayer::EndOfMedia:
                    playNext();
                    break;
                case QMediaPlayer::InvalidMedia:
                    skipBrokenTrack();
                    break;
                default:
                    break;
                }
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                if (error == QMediaPlayer::NoError) return;
                qWarning() << "[PlayerBackend] media error:" << error << errorString
                           << "path:" << m_currentPath;
                if (error == QMediaPlayer::ResourceError ||
                    error == QMediaPlayer::FormatError) {
                    skipBrokenTrack();
                }
            });

    m_libraryWatcher->start();

    QTimer::singleShot(0, this, [this]() {
        m_queue.setTracks(queryTracks());
        m_queueModel->resetAll();
        emit currentQueuePositionChanged();
    });
}

void PlayerBackend::initDatabase() {
    if (!MusicLibrary::initialize()) {
        qWarning() << "[PlayerBackend] DB initialization failed";
    }
}

void PlayerBackend::setupMpris() {
    new MprisRootAdaptor(this);
    auto *mprisPlayer = new MprisPlayerAdaptor(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this)) {
        qWarning() << "[MPRIS] registerObject failed";
        return;
    }
    if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.yamp"))) {
        qWarning() << "[MPRIS] registerService failed";
    }

    connect(mprisPlayer, &MprisPlayerAdaptor::nextRequested,      this, &PlayerBackend::playNext);
    connect(mprisPlayer, &MprisPlayerAdaptor::previousRequested,  this, &PlayerBackend::playPrevious);
    connect(mprisPlayer, &MprisPlayerAdaptor::playPauseRequested, this, &PlayerBackend::togglePlayback);
    connect(mprisPlayer, &MprisPlayerAdaptor::playRequested,      m_player, &QMediaPlayer::play);
    connect(mprisPlayer, &MprisPlayerAdaptor::pauseRequested,     m_player, &QMediaPlayer::pause);
    connect(mprisPlayer, &MprisPlayerAdaptor::stopRequested,      m_player, &QMediaPlayer::stop);
}

void PlayerBackend::setMuted(bool muted) {
    m_pwVolume->setMuted(muted);
}

void PlayerBackend::setVolume(qreal v) {
    m_pwVolume->setVolume(v);
}

void PlayerBackend::setShuffle(bool enabled) {
    if (m_queue.isShuffle() == enabled) return;
    m_queue.setShuffle(enabled);
    m_queueModel->resetAll();
    emit shuffleChanged();
    emit currentQueuePositionChanged();
}

void PlayerBackend::togglePlayback() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else if (m_player->hasAudio()) {
        m_player->play();
    }
}

QList<Track> PlayerBackend::queryTracks(const QString &whereClause, const QString &orderBy) {
    QList<Track> out;
    QString sql = QStringLiteral(
        "SELECT path, title, artist, album, duration, tech_info, track_no FROM tracks");
    if (!whereClause.isEmpty()) sql += QStringLiteral(" WHERE ") + whereClause;
    if (!orderBy.isEmpty()) sql += QStringLiteral(" ORDER BY ") + orderBy;
    else                    sql += QStringLiteral(" ORDER BY id");

    QSqlQuery q;
    q.setForwardOnly(true);
    if (!q.exec(sql)) {
        qWarning() << "[queryTracks] failed:" << q.lastError().text() << sql;
        return out;
    }
    while (q.next()) {
        Track t;
        t.path     = q.value(0).toString();
        t.title    = q.value(1).toString();
        t.artist   = q.value(2).toString();
        t.album    = q.value(3).toString();
        t.duration = q.value(4).toInt();
        t.techInfo = q.value(5).toString();
        t.trackNo  = q.value(6).toInt();
        out.append(t);
    }
    return out;
}

void PlayerBackend::rebuildQueueFromCurrentFilter() {
    const QString colName = sortColumnName(m_sortColumn);
    QString orderBy;
    if (!colName.isEmpty()) {
        orderBy = colName + (m_sortOrder == Qt::AscendingOrder
                             ? QStringLiteral(" ASC") : QStringLiteral(" DESC"));
    }
    m_queue.setTracks(queryTracks(combinedFilter(), orderBy));
    m_queueModel->resetAll();
}

void PlayerBackend::playMusic(const QString &filePath) {
    if (filePath.isEmpty()) return;
    const QString path = filePath.startsWith(QLatin1String("file://"))
                             ? QUrl(filePath).toLocalFile()
                             : filePath;

    rebuildQueueFromCurrentFilter();
    m_queue.setIndexByPath(path);
    m_queueModel->resetAll();
    loadTrack(m_queue.current());
}

void PlayerBackend::playFromQueue(int position) {
    m_queue.jumpToPosition(position);
    loadTrack(m_queue.current());
}

void PlayerBackend::playNext()     { loadTrack(m_queue.next()); }
void PlayerBackend::playPrevious() { loadTrack(m_queue.previous()); }

void PlayerBackend::skipBrokenTrack() {
    const int queueSize = m_queue.count();
    if (queueSize <= 0) {
        resetPlaybackState();
        return;
    }
    if (++m_consecutiveInvalid >= queueSize) {
        qWarning() << "[PlayerBackend] all queue items unplayable, stopping";
        m_consecutiveInvalid = 0;
        resetPlaybackState();
        return;
    }
    playNext();
}

QString PlayerBackend::coverCacheDir() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

QString PlayerBackend::coverPathForHash(const QByteArray &hash, const QString &ext) {
    return coverCacheDir() + QLatin1Char('/')
           + QString::fromLatin1(hash.toHex()) + QLatin1Char('.') + ext;
}

bool PlayerBackend::writeCoverAtomic(const QString &targetPath, const QByteArray &data) {
    if (QFileInfo::exists(targetPath)) return true;
    const QString tmpPath = targetPath + QStringLiteral(".tmp.")
                            + QString::number(QCoreApplication::applicationPid())
                            + QLatin1Char('.')
                            + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        if (tmp.write(data) != data.size()) {
            tmp.close();
            QFile::remove(tmpPath);
            return false;
        }
        tmp.flush();
        tmp.close();
    }
    if (QFileInfo::exists(targetPath)) {
        QFile::remove(tmpPath);
        return true;
    }
    if (!QFile::rename(tmpPath, targetPath)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

void PlayerBackend::pruneCoverCache(int keepCount) {
    const QString dir = coverCacheDir();
    QDir d(dir);
    const auto entries = d.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Time | QDir::Reversed);
    if (entries.size() <= keepCount) return;
    for (int i = 0; i < entries.size() - keepCount; ++i) {
        QFile::remove(entries[i].absoluteFilePath());
    }
}

void PlayerBackend::loadTrack(const Track &t) {
    if (!t.isValid()) return;

    m_currentIndex     = m_queue.currentGlobalId();
    m_currentPath      = t.path;
    m_currentTitle     = t.title;
    m_currentArtist    = t.artist;
    m_currentAlbum     = t.album;
    m_currentTechInfo  = t.techInfo;
    m_currentCoverPath.clear();

    m_player->setSource(QUrl::fromLocalFile(t.path));
    m_player->play();

    m_queueModel->notifyCurrentChanged();
    emit currentIndexChanged();
    emit currentQueuePositionChanged();
    emit metadataChanged();

    const QString trackPath = t.path;
    const quint64 gen = ++m_coverGen;
    QThreadPool::globalInstance()->start([this, trackPath, gen]() {
        const QByteArray data = CoverExtractor::embeddedPicture(trackPath);
        QString resolved;
        if (!data.isEmpty()) {
            const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
            const QString ext = CoverExtractor::detectImageExtension(data);
            const QString out = coverPathForHash(hash, ext);
            if (writeCoverAtomic(out, data)) {
                resolved = out;
                pruneCoverCache(256);
            }
        } else {
            resolved = CoverExtractor::sidecarImagePath(trackPath);
        }

        QMetaObject::invokeMethod(this, [this, trackPath, resolved, gen]() {
            if (gen != m_coverGen) return;
            if (m_currentPath != trackPath) return;
            m_currentCoverPath = resolved;
            emit metadataChanged();
        }, Qt::QueuedConnection);
    });
}

void PlayerBackend::scanFolder(const QUrl &folderUrl) {
    const QString path = folderUrl.toLocalFile();
    if (path.isEmpty()) return;

    auto *session = new ScanSession(path, this);
    m_scanProgresses.insert(session, qMakePair(0, 0));
    recomputeScanTotals();

    connect(session, &ScanSession::progressChanged, this, [this, session]() {
        auto it = m_scanProgresses.find(session);
        if (it == m_scanProgresses.end()) return;
        it->first  = session->processed();
        it->second = session->total();
        recomputeScanTotals();
    });

    connect(session, &ScanSession::batchReady, this, [this]() {
        if (!m_scanRefreshTimer->isActive()) m_scanRefreshTimer->start();
    });

    connect(session, &ScanSession::finished, this,
            [this, session](const QString &rootPath, const QList<Track> &newTracks) {
                if (m_scanRefreshTimer->isActive()) m_scanRefreshTimer->stop();
                m_queueModel->appendTracks(newTracks);
                refreshAllModels();
                if (m_libraryWatcher) m_libraryWatcher->registerScannedRoot(rootPath);
                m_scanProgresses.remove(session);
                recomputeScanTotals();
            });

    session->start();
}

QString PlayerBackend::combinedFilter() const {
    if (m_categoryFilter.isEmpty() && m_searchFilter.isEmpty()) return {};
    if (m_categoryFilter.isEmpty()) return m_searchFilter;
    if (m_searchFilter.isEmpty())   return m_categoryFilter;
    return QStringLiteral("(%1) AND (%2)").arg(m_categoryFilter, m_searchFilter);
}

void PlayerBackend::applyFilter() {
    m_trackModel->setFilter(combinedFilter());
    m_trackModel->setSort(m_sortColumn, m_sortOrder);
    m_trackModel->select();
}

void PlayerBackend::filterByAlbum(const QString &albumName, const QString &artistName) {
    if (albumName.isEmpty()) {
        m_categoryFilter.clear();
        m_sortColumn = TrackModel::TitleColumn;
        m_sortOrder  = Qt::AscendingOrder;
    } else {
        if (artistName.isEmpty()) {
            m_categoryFilter = QStringLiteral("album = %1").arg(sqlQuote(albumName));
        } else {
            m_categoryFilter = QStringLiteral(
                "album = %1 AND "
                "COALESCE(NULLIF(album_artist, ''), artist) = %2")
                .arg(sqlQuote(albumName), sqlQuote(artistName));
        }
        m_sortColumn = TrackModel::TrackNoColumn;
        m_sortOrder  = Qt::AscendingOrder;
    }
    applyFilter();
}

void PlayerBackend::filterByArtist(const QString &artistName) {
    if (artistName.isEmpty()) {
        m_categoryFilter.clear();
        m_sortColumn = TrackModel::TitleColumn;
        m_sortOrder  = Qt::AscendingOrder;
    } else {
        const QString norm = MusicLibrary::normalizeArtistName(artistName);
        m_categoryFilter = QStringLiteral(
            "id IN (SELECT track_id FROM track_artists ta "
            "JOIN artists a ON a.id = ta.artist_id "
            "WHERE a.name_norm = %1)").arg(sqlQuote(norm));
        m_sortColumn = TrackModel::AlbumColumn;
        m_sortOrder  = Qt::AscendingOrder;
    }
    applyFilter();
}

void PlayerBackend::searchTracks(const QString &query) {
    if (query.isEmpty()) {
        m_searchFilter.clear();
    } else {
        const QString safe = sqlLikeEscape(query.toLower());
        m_searchFilter = QStringLiteral("search_text LIKE '%%%1%%' ESCAPE '\\'").arg(safe);
    }
    applyFilter();
}

void PlayerBackend::searchAlbums(const QString &query) {
    m_albumModel->setSearch(query);
}

void PlayerBackend::searchArtists(const QString &query) {
    m_artistModel->setSearch(query);
}

void PlayerBackend::sortTracks(int column, bool ascending) {
    m_sortColumn = column;
    m_sortOrder  = ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_trackModel->setSort(m_sortColumn, m_sortOrder);
    m_trackModel->select();
}

int PlayerBackend::getRowForPath(const QString &path) {
    if (path.isEmpty()) return -1;

    QString colName = sortColumnName(m_sortColumn);
    if (colName.isEmpty()) colName = QStringLiteral("id");
    const QString order = m_sortOrder == Qt::AscendingOrder
                              ? QStringLiteral("ASC") : QStringLiteral("DESC");
    const QString filter = combinedFilter();

    QString sql = QStringLiteral(
        "WITH ranked AS (SELECT path, "
        "ROW_NUMBER() OVER (ORDER BY %1 %2, id ASC) - 1 AS rn "
        "FROM tracks").arg(colName, order);
    if (!filter.isEmpty()) sql += QStringLiteral(" WHERE ") + filter;
    sql += QStringLiteral(") SELECT rn FROM ranked WHERE path = ?");

    QSqlQuery q;
    q.prepare(sql);
    q.addBindValue(path);
    if (!q.exec()) {
        qWarning() << "[getRowForPath] SQL error:" << q.lastError().text();
        return -1;
    }
    if (!q.next()) return -1;
    return q.value(0).toInt();
}

void PlayerBackend::addPlayNext(const QString &path) {
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT title, artist, album, duration, tech_info, track_no "
        "FROM tracks WHERE path = ?"));
    q.addBindValue(path);

    if (!q.exec()) {
        qWarning() << "[addPlayNext] SQL error:" << q.lastError().text();
        return;
    }
    if (!q.next()) {
        qWarning() << "[addPlayNext] track not in DB:" << path;
        return;
    }
    Track t;
    t.path     = path;
    t.title    = q.value(0).toString();
    t.artist   = q.value(1).toString();
    t.album    = q.value(2).toString();
    t.duration = q.value(3).toInt();
    t.techInfo = q.value(4).toString();
    t.trackNo  = q.value(5).toInt();
    m_queueModel->insertTrack(t);
}

void PlayerBackend::openInFileManager(const QString &path) {
    if (path.isEmpty()) return;
    QFileInfo info(path);
    if (!info.exists()) return;

    const QString uri = QUrl::fromLocalFile(path).toString();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("ShowItems"));
    msg << QStringList{uri} << QString();

    if (!QDBusConnection::sessionBus().send(msg)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}

void PlayerBackend::refreshAlbumModel() {
    m_albumModel->refresh();
}

void PlayerBackend::refreshArtistModel() {
    m_artistModel->refresh();
}

void PlayerBackend::refreshAllModels() {
    m_trackModel->select();
    refreshAlbumModel();
    refreshArtistModel();
}

void PlayerBackend::resetPlaybackState() {
    m_player->stop();
    m_player->setSource(QUrl());
    m_currentPath.clear();
    m_currentTitle    = kDefaultTitle;
    m_currentArtist   = kDefaultArtist;
    m_currentAlbum    = kDefaultAlbum;
    m_currentTechInfo.clear();
    m_currentCoverPath.clear();
    m_currentIndex    = -1;
    emit metadataChanged();
    emit currentIndexChanged();
}

void PlayerBackend::recomputeScanTotals() {
    int progress = 0;
    int total    = 0;
    for (auto it = m_scanProgresses.cbegin(); it != m_scanProgresses.cend(); ++it) {
        progress += it.value().first;
        total    += it.value().second;
    }
    if (progress == m_scanProgressCached && total == m_scanTotalCached) return;
    m_scanProgressCached = progress;
    m_scanTotalCached    = total;
    emit scanProgressChanged();
}

void PlayerBackend::clearLibrary() {
    resetPlaybackState();

    m_queue.setTracks({});
    m_queueModel->resetAll();

    m_libraryWatcher->clearAll();

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM tracks"));
    q.exec(QStringLiteral("DELETE FROM track_artists"));
    q.exec(QStringLiteral("DELETE FROM artists"));
    q.exec(QStringLiteral("DELETE FROM watch_roots"));
    db.commit();

    m_categoryFilter.clear();
    m_searchFilter.clear();
    m_trackModel->setFilter(QString());
    refreshAllModels();

    emit currentQueuePositionChanged();
}

void PlayerBackend::removeFolder(const QString &folder) {
    if (folder.isEmpty()) return;
    const QString clean = QDir(folder).absolutePath();
    if (clean.isEmpty()) return;

    m_libraryWatcher->removeRoot(clean);

    {
        QSqlDatabase db = QSqlDatabase::database();
        db.transaction();
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ? OR path LIKE ?"));
        del.addBindValue(clean);
        del.addBindValue(clean + QStringLiteral("/%"));
        if (!del.exec()) {
            qWarning() << "[removeFolder] delete tracks failed:" << del.lastError().text();
        }
        db.commit();
        MusicLibrary::pruneOrphanArtists(db);
    }

    const QString prefix = clean + QLatin1Char('/');
    const bool currentInRemoved = !m_currentPath.isEmpty() &&
                                  (m_currentPath == clean || m_currentPath.startsWith(prefix));
    if (currentInRemoved) {
        resetPlaybackState();
    }

    refreshAllModels();

    rebuildQueueFromCurrentFilter();
    emit currentQueuePositionChanged();
}

void PlayerBackend::syncWithFolders(const QStringList &folders) {
    QSet<QString> desired;
    for (const QString &raw : folders) {
        const QString clean = QDir(raw).absolutePath();
        if (!clean.isEmpty() && QDir(clean).exists()) desired.insert(clean);
    }

    const QStringList currentRoots = m_libraryWatcher->roots();
    const QSet<QString> currentSet(currentRoots.begin(), currentRoots.end());

    for (const QString &existing : currentSet) {
        if (!desired.contains(existing)) removeFolder(existing);
    }
    for (const QString &want : desired) {
        if (!currentSet.contains(want)) scanFolder(QUrl::fromLocalFile(want));
    }
}
