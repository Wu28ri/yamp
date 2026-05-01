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

    m_queueModel = new QueueModel(&m_queue, this);

    m_libraryWatcher = new LibraryWatcher(this);
    connect(m_libraryWatcher, &LibraryWatcher::libraryChanged, this, [this]() {
        m_trackModel->select();
        refreshAlbumModel();
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
    if (qFuzzyCompare(m_audioOutput->volume(), static_cast<float>(v))) return;
    m_audioOutput->setVolume(v);
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
    connect(scanner, &LibraryScanner::finished, this, [this, path, scanner, thread](const QList<Track> &newTracks) {
        for (const Track &t : newTracks) m_queue.addTrack(t);
        m_queueModel->resetAll();
        m_trackModel->select();
        refreshAlbumModel();
        if (m_libraryWatcher) m_libraryWatcher->addRoot(path);
        thread->quit();
        scanner->deleteLater();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}

void PlayerBackend::filterByAlbum(const QString &albumName) {
    if (albumName.isEmpty()) {
        m_filterClause.clear();
        m_trackModel->setFilter({});
        m_trackModel->setSort(TrackModel::TitleColumn, Qt::AscendingOrder);
    } else {
        QString safe = albumName;
        safe.replace(QLatin1Char('\''), QLatin1String("''"));
        m_filterClause = QStringLiteral("album = '%1'").arg(safe);
        m_trackModel->setFilter(m_filterClause);
        m_trackModel->setSort(TrackModel::TrackNoColumn, Qt::AscendingOrder);
    }
    m_trackModel->select();
}

void PlayerBackend::searchTracks(const QString &query) {
    if (query.isEmpty()) {
        m_filterClause.clear();
        m_trackModel->setFilter({});
    } else {
        QString safe = query.toLower();
        safe.replace(QLatin1Char('\''), QLatin1String("''"));
        m_filterClause = QStringLiteral("search_text LIKE '%%%1%%'").arg(safe);
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
