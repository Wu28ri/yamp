#include "PlayerBackend.h"

#include "CoverExtractor.h"
#include "MprisAdaptor.h"
#include "MusicLibrary.h"

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

PlayerBackend::PlayerBackend(QObject *parent)
    : QObject(parent),
      m_currentTitle(QStringLiteral("N/A")),
      m_currentArtist(QStringLiteral("Unknown Artist")),
      m_currentAlbum(QStringLiteral("Unknown Album")) {

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
    connect(m_libraryWatcher, &LibraryWatcher::libraryChanged, this, [this]() {
        m_trackModel->select();
        refreshAlbumModel();
        refreshArtistModel();
    });

    m_player      = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    setupMpris();

    connect(m_player, &QMediaPlayer::positionChanged,      this, &PlayerBackend::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,      this, &PlayerBackend::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PlayerBackend::playbackStateChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged,   this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia) playNext();
            });

    m_libraryWatcher->start();

    m_queue.setTracks(queryTracks());
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
    if (m_audioOutput->isMuted() == muted) return;
    m_audioOutput->setMuted(muted);
    emit mutedChanged();
}

void PlayerBackend::setVolume(qreal v) {
    const float curr = m_audioOutput->volume();
    const float next = static_cast<float>(v);
    const bool same = (qFuzzyIsNull(curr) && qFuzzyIsNull(next))
                       || qFuzzyCompare(curr, next);
    if (same) return;
    m_audioOutput->setVolume(next);
    emit volumeChanged();
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

QList<Track> PlayerBackend::queryTracks(const QString &whereClause) {
    QList<Track> out;
    QString sql = QStringLiteral(
        "SELECT path, title, artist, album, duration, tech_info, track_no FROM tracks");
    if (!whereClause.isEmpty()) sql += QStringLiteral(" WHERE ") + whereClause;
    sql += QStringLiteral(" ORDER BY id");

    QSqlQuery q;
    q.setForwardOnly(true);
    if (!q.exec(sql)) {
        qWarning() << "[queryTracks] failed:" << q.lastError().text() << sql;
        return out;
    }
    while (q.next()) {
        out.append({
            q.value(0).toString(),
            q.value(1).toString(),
            q.value(2).toString(),
            q.value(3).toString(),
            q.value(4).toInt(),
            q.value(5).toString(),
            q.value(6).toInt(),
        });
    }
    return out;
}

void PlayerBackend::rebuildQueueFromCurrentFilter() {
    while (m_trackModel->canFetchMore()) {
        m_trackModel->fetchMore();
    }

    QList<Track> tracks;
    const int rows = m_trackModel->rowCount();
    tracks.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        tracks.append({
            m_trackModel->data(m_trackModel->index(i, TrackModel::PathColumn)).toString(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::TitleColumn)).toString(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::ArtistColumn)).toString(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::AlbumColumn)).toString(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::DurationColumn)).toInt(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::TechInfoColumn)).toString(),
            m_trackModel->data(m_trackModel->index(i, TrackModel::TrackNoColumn)).toInt()
        });
    }

    m_queue.setTracks(tracks);
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

QString PlayerBackend::tempCoverPathForExt(const QString &ext) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/current_art.") + ext;
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
    QThreadPool::globalInstance()->start([this, trackPath]() {
        const QByteArray data = CoverExtractor::embeddedPicture(trackPath);
        QString resolved;
        if (!data.isEmpty()) {
            const QString ext = CoverExtractor::detectImageExtension(data);
            const QString out = tempCoverPathForExt(ext);
            QFile f(out);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(data);
                f.close();
                resolved = out;
            }
        } else {
            resolved = CoverExtractor::sidecarImagePath(trackPath);
        }

        QMetaObject::invokeMethod(this, [this, trackPath, resolved]() {
            if (m_currentPath == trackPath) {
                m_currentCoverPath = resolved;
                emit metadataChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void PlayerBackend::scanFolder(const QUrl &folderUrl) {
    const QString path = folderUrl.toLocalFile();
    if (path.isEmpty()) return;

    auto *thread = new QThread(this);
    auto *scanner = new LibraryScanner(path);
    scanner->moveToThread(thread);

    connect(thread, &QThread::started, scanner, &LibraryScanner::run);
    connect(scanner, &LibraryScanner::finished, this,
            [this, path, thread](const QList<Track> &newTracks) {
                for (const Track &t : newTracks) m_queue.addTrack(t);
                m_queueModel->resetAll();
                m_trackModel->select();
                refreshAlbumModel();
                refreshArtistModel();
                if (m_libraryWatcher) m_libraryWatcher->addRoot(path);
                thread->quit();
            });
    connect(thread, &QThread::finished, scanner, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread,  &QObject::deleteLater);

    thread->start();
}

void PlayerBackend::filterByAlbum(const QString &albumName, const QString &artistName) {
    if (albumName.isEmpty()) {
        m_filterClause.clear();
        m_trackModel->setFilter({});
        m_trackModel->setSort(TrackModel::TitleColumn, Qt::AscendingOrder);
    } else {
        QString safeAlbum = albumName;
        safeAlbum.replace(QLatin1Char('\''), QLatin1String("''"));
        if (artistName.isEmpty()) {
            m_filterClause = QStringLiteral("album = '%1'").arg(safeAlbum);
        } else {
            QString safeArtist = artistName;
            safeArtist.replace(QLatin1Char('\''), QLatin1String("''"));
            m_filterClause = QStringLiteral("album = '%1' AND artist = '%2'")
                                 .arg(safeAlbum, safeArtist);
        }
        m_trackModel->setFilter(m_filterClause);
        m_trackModel->setSort(TrackModel::TrackNoColumn, Qt::AscendingOrder);
    }
    m_trackModel->select();
}

void PlayerBackend::filterByArtist(const QString &artistName) {
    if (artistName.isEmpty()) {
        m_filterClause.clear();
        m_trackModel->setFilter({});
        m_trackModel->setSort(TrackModel::TitleColumn, Qt::AscendingOrder);
    } else {
        QString norm = MusicLibrary::normalizeArtistName(artistName);
        norm.replace(QLatin1Char('\''), QLatin1String("''"));
        m_filterClause = QStringLiteral(
            "id IN (SELECT track_id FROM track_artists ta "
            "JOIN artists a ON a.id = ta.artist_id "
            "WHERE a.name_norm = '%1')").arg(norm);
        m_trackModel->setFilter(m_filterClause);
        m_trackModel->setSort(TrackModel::AlbumColumn, Qt::AscendingOrder);
    }
    m_trackModel->select();
}

void PlayerBackend::searchTracks(const QString &query) {
    if (query.isEmpty()) {
        m_filterClause.clear();
        m_trackModel->setFilter({});
    } else {
        QString safe = query.toLower();
        safe.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
        safe.replace(QLatin1Char('%'),  QLatin1String("\\%"));
        safe.replace(QLatin1Char('_'),  QLatin1String("\\_"));
        safe.replace(QLatin1Char('\''), QLatin1String("''"));
        m_filterClause = QStringLiteral("search_text LIKE '%%%1%%' ESCAPE '\\'").arg(safe);
        m_trackModel->setFilter(m_filterClause);
    }
    m_trackModel->select();
}

void PlayerBackend::sortTracks(int column, bool ascending) {
    m_trackModel->setSort(column, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    m_trackModel->select();
}

int PlayerBackend::getRowForPath(const QString &path) {
    return m_trackModel->rowForPath(path);
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
    m_queueModel->insertTrack({
        path,
        q.value(0).toString(),
        q.value(1).toString(),
        q.value(2).toString(),
        q.value(3).toInt(),
        q.value(4).toString(),
        q.value(5).toInt(),
    });
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

void PlayerBackend::clearLibrary() {
    m_player->stop();
    m_player->setSource(QUrl());

    m_currentPath.clear();
    m_currentTitle    = QStringLiteral("N/A");
    m_currentArtist   = QStringLiteral("Unknown Artist");
    m_currentAlbum    = QStringLiteral("Unknown Album");
    m_currentTechInfo.clear();
    m_currentCoverPath.clear();
    m_currentIndex    = -1;

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

    m_filterClause.clear();
    m_trackModel->setFilter(QString());
    m_trackModel->select();
    refreshAlbumModel();
    refreshArtistModel();

    emit metadataChanged();
    emit currentIndexChanged();
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
    }

    const QString prefix = clean + QLatin1Char('/');
    const bool currentInRemoved = !m_currentPath.isEmpty() &&
                                  (m_currentPath == clean || m_currentPath.startsWith(prefix));
    if (currentInRemoved) {
        m_player->stop();
        m_player->setSource(QUrl());
        m_currentPath.clear();
        m_currentTitle    = QStringLiteral("N/A");
        m_currentArtist   = QStringLiteral("Unknown Artist");
        m_currentAlbum    = QStringLiteral("Unknown Album");
        m_currentTechInfo.clear();
        m_currentCoverPath.clear();
        m_currentIndex    = -1;
        emit metadataChanged();
        emit currentIndexChanged();
    }

    m_trackModel->select();
    refreshAlbumModel();
    refreshArtistModel();

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
