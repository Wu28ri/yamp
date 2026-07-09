#include "PlayerBackend.h"

#include "AlsaDevicesService.h"
#include "CoverCacheService.h"
#include "LibraryWatcher.h"
#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "PaVolumeController.h"
#include "ScanCoordinator.h"
#include "SqlUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlQuery>
#include <QTimer>
#include <QUrl>

#include <clocale>
#include <cstring>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

#include <mpv/client.h>

namespace {
const QString kDefaultTitle  = QStringLiteral("N/A");
const QString kDefaultArtist = QStringLiteral("Unknown Artist");
const QString kDefaultAlbum  = QStringLiteral("Unknown Album");

const QString kColumnTitle    = QStringLiteral("title");
const QString kColumnArtist   = QStringLiteral("artist");
const QString kColumnAlbum    = QStringLiteral("album");
const QString kColumnDuration = QStringLiteral("duration");
const QString kColumnTrackNo  = QStringLiteral("track_no");

bool isValidSortColumn(const QString &name) {
    return name == kColumnTitle    || name == kColumnArtist
        || name == kColumnAlbum    || name == kColumnDuration
        || name == kColumnTrackNo;
}

Track trackFromQuery(const QSqlQuery &q, int offset = 0) {
    Track t;
    t.path        = q.value(offset + 0).toString();
    t.title       = q.value(offset + 1).toString();
    t.artist      = q.value(offset + 2).toString();
    t.album       = q.value(offset + 3).toString();
    t.duration    = q.value(offset + 4).toInt();
    t.techInfo    = q.value(offset + 5).toString();
    t.trackNo     = q.value(offset + 6).toInt();
    t.albumArtist = q.value(offset + 7).toString();
    return t;
}
}

PlayerBackend::PlayerBackend(QObject *parent)
    : QObject(parent),
      m_currentTitle(kDefaultTitle),
      m_currentArtist(kDefaultArtist),
      m_currentAlbum(kDefaultAlbum) {

    MusicLibrary::initialize();

    m_trackModel = new TrackModel(this);
    m_trackModel->setTable(QStringLiteral("tracks"));
    m_trackModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_trackModel->setSort(m_trackModel->columnFor(m_sortColumn), m_sortOrder);
    m_trackModel->select();

    m_albumModel = new AlbumModel(this);
    m_albumModel->refresh();

    m_artistModel = new ArtistModel(this);
    m_artistModel->refresh();

    m_queueModel = new QueueModel(&m_queue, this);

    m_libraryWatcher = new LibraryWatcher(this);
    connect(m_libraryWatcher, &LibraryWatcher::libraryChanged, this,
            &PlayerBackend::refreshAllModels);

    m_scanRefreshTimer = new QTimer(this);
    m_scanRefreshTimer->setSingleShot(true);
    m_scanRefreshTimer->setInterval(750);
    connect(m_scanRefreshTimer, &QTimer::timeout, this,
            &PlayerBackend::refreshAllModels);

    m_scanCoordinator = new ScanCoordinator(m_libraryWatcher, this);
    connect(m_scanCoordinator, &ScanCoordinator::progressChanged,
            this, &PlayerBackend::scanProgressChanged);
    connect(m_scanCoordinator, &ScanCoordinator::newTracksAvailable, this,
            [this](const QList<Track> &newTracks) {
                m_scanRefreshTimer->stop();
                m_queueModel->appendTracks(newTracks);
                refreshAllModels();
            });
    connect(m_scanCoordinator, &ScanCoordinator::batchReady, this, [this]() {
        if (!m_scanRefreshTimer->isActive()) m_scanRefreshTimer->start();
    });

    m_coverService = new CoverCacheService(this);
    connect(m_coverService, &CoverCacheService::coverResolved, this,
            [this](const QString &trackPath, const QString &coverPath, quint64 generation) {
                if (generation != m_coverGen) return;
                if (m_currentPath != trackPath) return;
                m_currentCoverPath = coverPath;
                emit metadataChanged();
            });

    initMpv();

    m_paVolume = new PaVolumeController(this);
    connect(m_paVolume, &PaVolumeController::volumeChanged,
            this, [this]() { if (!m_bitPerfectEnabled) emit volumeChanged(); });
    connect(m_paVolume, &PaVolumeController::mutedChanged,
            this, [this]() { if (!m_bitPerfectEnabled) emit mutedChanged(); });

    setupMpris();

    m_positionPollTimer = new QTimer(this);
    m_positionPollTimer->setInterval(100);
    connect(m_positionPollTimer, &QTimer::timeout, this, [this]() {
        if (!m_mpv) return;
        double t = 0;
        if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &t) >= 0) {
            const qint64 ms = static_cast<qint64>(t * 1000.0);
            if (ms != m_lastPositionMs) {
                m_lastPositionMs = ms;
                emit positionChanged();
            }
        }
    });

    m_libraryWatcher->start();

    QTimer::singleShot(0, this, [this]() {
        rebuildQueueFromCurrentFilter();
        emit currentQueuePositionChanged();
    });
}

PlayerBackend::~PlayerBackend() {
    shutdownMpv();
}

bool PlayerBackend::scanInProgress() const { return m_scanCoordinator && m_scanCoordinator->active(); }
int  PlayerBackend::scanProgress()   const { return m_scanCoordinator ? m_scanCoordinator->progress() : 0; }
int  PlayerBackend::scanTotal()      const { return m_scanCoordinator ? m_scanCoordinator->total()    : 0; }

void PlayerBackend::initMpv() {
    setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        qCritical("[mpv] mpv_create failed; playback disabled");
        return;
    }

    mpv_set_option_string(m_mpv, "vid",                "no");
    mpv_set_option_string(m_mpv, "audio-display",      "no");
    mpv_set_option_string(m_mpv, "idle",               "yes");
    mpv_set_option_string(m_mpv, "force-window",       "no");
    mpv_set_option_string(m_mpv, "terminal",           "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard",  "no");
    mpv_set_option_string(m_mpv, "osc",                "no");
    mpv_set_option_string(m_mpv, "keep-open",          "no");
    mpv_set_option_string(m_mpv, "audio-client-name",  "yamp");
    mpv_set_option_string(m_mpv, "volume",             "100");
    mpv_set_option_string(m_mpv, "replaygain",         "no");
    mpv_set_option_string(m_mpv, "replaygain-preamp",  "0");
    mpv_set_option_string(m_mpv, "replaygain-clip",    "yes");

    if (mpv_initialize(m_mpv) < 0) {
        qCritical("[mpv] mpv_initialize failed; playback disabled");
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "pause",       MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "duration",    MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);

    mpv_set_wakeup_callback(m_mpv, &PlayerBackend::mpvWakeupCallback, this);
}

void PlayerBackend::shutdownMpv() {
    if (m_positionPollTimer) m_positionPollTimer->stop();
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void PlayerBackend::mpvWakeupCallback(void *ctx) {
    auto *self = static_cast<PlayerBackend*>(ctx);
    QMetaObject::invokeMethod(self, "processMpvEvents", Qt::QueuedConnection);
}

void PlayerBackend::processMpvEvents() {
    if (!m_mpv) return;
    while (true) {
        mpv_event *e = mpv_wait_event(m_mpv, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            handleMpvPropertyChange(static_cast<mpv_event_property*>(e->data));
            break;
        case MPV_EVENT_END_FILE:
            handleMpvEndFile(static_cast<mpv_event_end_file*>(e->data));
            break;
        default:
            break;
        }
    }
}

void PlayerBackend::handleMpvPropertyChange(mpv_event_property *prop) {
    if (!prop || !prop->name) return;

    if (std::strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
        const bool newPaused = *static_cast<int*>(prop->data) != 0;
        if (newPaused != m_paused) {
            m_paused = newPaused;
            if (m_paused) {
                m_positionPollTimer->stop();
            } else if (m_hasFile) {
                m_positionPollTimer->start();
            }
            emit playbackStateChanged();
        }
    } else if (std::strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
        const double d = *static_cast<double*>(prop->data);
        const qint64 ms = static_cast<qint64>(d * 1000.0);
        if (ms != m_durationMs) {
            m_durationMs = ms;
            emit durationChanged();
        }
    } else if (std::strcmp(prop->name, "idle-active") == 0 && prop->format == MPV_FORMAT_FLAG) {
        const bool idle = *static_cast<int*>(prop->data) != 0;
        const bool newHasFile = !idle;
        if (newHasFile != m_hasFile) {
            m_hasFile = newHasFile;
            if (!m_hasFile) {
                m_positionPollTimer->stop();
                if (m_durationMs != 0) {
                    m_durationMs = 0;
                    emit durationChanged();
                }
                if (m_lastPositionMs != 0) {
                    m_lastPositionMs = 0;
                    emit positionChanged();
                }
            } else if (!m_paused) {
                m_positionPollTimer->start();
            }
            emit playbackStateChanged();
        }
    }
}

void PlayerBackend::handleMpvEndFile(mpv_event_end_file *ev) {
    if (!ev) return;
    switch (ev->reason) {
    case MPV_END_FILE_REASON_EOF:
        playNext();
        break;
    case MPV_END_FILE_REASON_ERROR:
        resetPlaybackState();
        break;
    default:
        break;
    }
}

void PlayerBackend::setupMpris() {
    new MprisRootAdaptor(this);
    auto *mprisPlayer = new MprisPlayerAdaptor(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this)) return;
    bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.yamp"));

    connect(mprisPlayer, &MprisPlayerAdaptor::nextRequested,      this, &PlayerBackend::playNext);
    connect(mprisPlayer, &MprisPlayerAdaptor::previousRequested,  this, &PlayerBackend::playPrevious);
    connect(mprisPlayer, &MprisPlayerAdaptor::playPauseRequested, this, &PlayerBackend::togglePlayback);
    connect(mprisPlayer, &MprisPlayerAdaptor::playRequested,      this, &PlayerBackend::play);
    connect(mprisPlayer, &MprisPlayerAdaptor::pauseRequested,     this, &PlayerBackend::pause);
    connect(mprisPlayer, &MprisPlayerAdaptor::stopRequested,      this, &PlayerBackend::stop);
}

void PlayerBackend::setMuted(bool muted) {
    if (usingSoftwareVolume()) {
        if (m_swMuted == muted) return;
        m_swMuted = muted;
        applyMpvVolume();
        emit mutedChanged();
        return;
    }
    if (m_bitPerfectEnabled) return;
    m_paVolume->setMuted(muted);
}

void PlayerBackend::setVolume(qreal v) {
    v = qBound(0.0, v, 1.0);
    if (usingSoftwareVolume()) {
        if (qFuzzyCompare(m_swVolume + 1.0, v + 1.0)) return;
        m_swVolume = v;
        applyMpvVolume();
        emit volumeChanged();
        return;
    }
    if (m_bitPerfectEnabled) return;
    m_paVolume->setVolume(v);
}

bool PlayerBackend::isMuted() const {
    if (m_bitPerfectEnabled) return m_softwareVolume ? m_swMuted : false;
    return m_paVolume ? m_paVolume->isMuted() : false;
}

qreal PlayerBackend::volume() const {
    if (m_bitPerfectEnabled) return m_softwareVolume ? m_swVolume : 1.0;
    return m_paVolume ? m_paVolume->volume() : m_swVolume;
}

void PlayerBackend::setBitPerfect(bool enabled) {
    if (m_bitPerfectEnabled == enabled) return;
    m_bitPerfectEnabled = enabled;
    if (enabled && m_paVolume) {
        m_swVolume = m_paVolume->volume();
        m_swMuted  = m_paVolume->isMuted();
    }
    applyAudioDeviceToMpv();
    applyMpvVolume();
    emit bitPerfectChanged();
    emit volumeControllableChanged();
    emit volumeChanged();
    emit mutedChanged();
}

void PlayerBackend::setAudioDevice(const QString &device) {
    const QString normalized = device.isEmpty() ? QStringLiteral("auto") : device;
    if (m_audioDevice == normalized) return;
    m_audioDevice = normalized;
    if (m_bitPerfectEnabled) applyAudioDeviceToMpv();
    emit audioDeviceChanged();
}

void PlayerBackend::setSoftwareVolume(bool enabled) {
    if (m_softwareVolume == enabled) return;
    m_softwareVolume = enabled;
    if (enabled && m_paVolume && !m_bitPerfectEnabled) {
        m_swVolume = m_paVolume->volume();
        m_swMuted  = m_paVolume->isMuted();
    }
    applyMpvVolume();
    emit softwareVolumeChanged();
    emit volumeControllableChanged();
    emit volumeChanged();
    emit mutedChanged();
}

void PlayerBackend::applyAudioDeviceToMpv() {
    if (!m_mpv) return;
    const QString eff = (m_bitPerfectEnabled && m_audioDevice != QStringLiteral("auto"))
        ? m_audioDevice
        : QStringLiteral("auto");
    mpv_set_property_string(m_mpv, "audio-device", eff.toUtf8().constData());
}

void PlayerBackend::applyMpvVolume() {
    if (!m_mpv) return;
    const bool sw = usingSoftwareVolume();
    double vol = sw ? (m_swVolume * 100.0) : 100.0;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    int mute = (sw && m_swMuted) ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &mute);
}

QVariantList PlayerBackend::listHardwareDevices() {
    return AlsaDevices::list();
}

bool PlayerBackend::lockDeviceToZeroDb() {
    return AlsaDevices::lockToZeroDb(m_audioDevice);
}

void PlayerBackend::setShuffle(bool enabled) {
    if (m_queue.isShuffle() == enabled) return;
    m_queue.setShuffle(enabled);
    m_queueModel->resetAll();
    emit shuffleChanged();
    emit currentQueuePositionChanged();
}

void PlayerBackend::setReplayGainEnabled(bool enabled) {
    if (m_rgEnabled == enabled) return;
    m_rgEnabled = enabled;
    applyReplayGainSettings();
}

void PlayerBackend::setReplayGainMode(int mode) {
    if (mode != ReplayGainMode::RgModeTrack && mode != ReplayGainMode::RgModeAlbum) return;
    if (m_rgMode == mode) return;
    m_rgMode = mode;
    applyReplayGainSettings();
}

void PlayerBackend::setReplayGainPreampDb(qreal db) {
    db = qBound(Settings::kReplayGainPreampMinDb, db, Settings::kReplayGainPreampMaxDb);
    if (qFuzzyCompare(m_rgPreampDb + 100.0, db + 100.0)) return;
    m_rgPreampDb = db;
    applyReplayGainSettings();
}

void PlayerBackend::setReplayGainClipProtect(bool enabled) {
    if (m_rgClipProtect == enabled) return;
    m_rgClipProtect = enabled;
    applyReplayGainSettings();
}

void PlayerBackend::applyReplayGainSettings() {
    if (!m_mpv) return;

    const char *mode = "no";
    if (m_rgEnabled) mode = (m_rgMode == ReplayGainMode::RgModeAlbum) ? "album" : "track";
    mpv_set_property_string(m_mpv, "replaygain", mode);

    double preamp = m_rgPreampDb;
    mpv_set_property(m_mpv, "replaygain-preamp", MPV_FORMAT_DOUBLE, &preamp);

    mpv_set_property_string(m_mpv, "replaygain-clip", m_rgClipProtect ? "yes" : "no");
}

qint64 PlayerBackend::position() const {
    if (!m_mpv) return 0;
    double t = 0;
    if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &t) >= 0) {
        return static_cast<qint64>(t * 1000.0);
    }
    return 0;
}

void PlayerBackend::setPosition(qint64 ms) {
    if (!m_mpv) return;
    if (ms < 0) ms = 0;
    double t = ms / 1000.0;
    if (mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &t) >= 0) {
        m_lastPositionMs = ms;
        emit positionChanged();
    }
}

void PlayerBackend::play() {
    if (!m_mpv) return;
    int p = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
}

void PlayerBackend::pause() {
    if (!m_mpv) return;
    int p = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
}

void PlayerBackend::stop() {
    if (!m_mpv) return;
    const char *cmd[] = {"stop", nullptr};
    mpv_command(m_mpv, cmd);
}

void PlayerBackend::togglePlayback() {
    if (!m_mpv) return;
    if (m_hasFile) {
        int p = m_paused ? 0 : 1;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
        return;
    }
    if (m_queue.count() > 0) playFromQueue(0);
}

QList<Track> PlayerBackend::queryTracks(const QString &whereClause, const QString &orderBy) {
    QList<Track> out;
    QString sql = QStringLiteral(
        "SELECT path, title, artist, album, duration, tech_info, track_no, album_artist "
        "FROM tracks");
    if (!whereClause.isEmpty()) sql += QStringLiteral(" WHERE ") + whereClause;
    sql += QStringLiteral(" ORDER BY ") + (orderBy.isEmpty() ? QStringLiteral("id") : orderBy);

    QSqlQuery q;
    q.setForwardOnly(true);
    if (!q.exec(sql)) return out;
    while (q.next()) out.append(trackFromQuery(q));
    return out;
}

void PlayerBackend::rebuildQueueFromCurrentFilter() {
    QString orderBy;
    if (!m_sortColumn.isEmpty()) {
        orderBy = m_sortColumn + (m_sortOrder == Qt::AscendingOrder
                                  ? QStringLiteral(" ASC") : QStringLiteral(" DESC"));
    }
    m_queue.setTracks(queryTracks(m_categoryFilter, orderBy));
    m_queueBuiltFromFilter = m_categoryFilter;
    m_queueBuiltFromSort   = m_sortColumn;
    m_queueBuiltFromOrder  = m_sortOrder;
    m_queueModel->resetAll();
}

void PlayerBackend::playMusic(const QString &filePath) {
    if (filePath.isEmpty()) return;
    const QString path = filePath.startsWith(QLatin1String("file://"))
                             ? QUrl(filePath).toLocalFile()
                             : filePath;

    const bool queueMatchesView = (m_queueBuiltFromFilter == m_categoryFilter &&
                                   m_queueBuiltFromSort   == m_sortColumn   &&
                                   m_queueBuiltFromOrder  == m_sortOrder);
    const int existing = queueMatchesView ? m_queue.positionOfPath(path) : -1;
    if (existing >= 0) {
        m_queue.jumpToPosition(existing);
    } else {
        rebuildQueueFromCurrentFilter();
        m_queue.setIndexByPath(path);
        m_queueModel->resetAll();
    }
    loadTrack(m_queue.current());
}

void PlayerBackend::playFromQueue(int position) {
    m_queue.jumpToPosition(position);
    loadTrack(m_queue.current());
}

void PlayerBackend::playNext()     { loadTrack(m_queue.next()); }
void PlayerBackend::playPrevious() { loadTrack(m_queue.previous()); }

void PlayerBackend::loadTrack(const Track &t) {
    if (!t.isValid()) return;

    m_currentTrack    = t;
    m_currentPath     = t.path;
    m_currentTitle    = t.title;
    m_currentArtist   = t.artist;
    m_currentAlbum    = t.album;
    m_currentTechInfo = t.techInfo;
    m_currentCoverPath.clear();

    if (m_mpv) {
        int p = 0;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
        const QByteArray path = t.path.toUtf8();
        const char *cmd[] = {"loadfile", path.constData(), nullptr};
        mpv_command(m_mpv, cmd);
    }

    m_queueModel->notifyCurrentChanged();
    emit currentIndexChanged();
    emit currentQueuePositionChanged();
    emit metadataChanged();

    m_coverService->requestCoverFor(t.path, ++m_coverGen);
}

void PlayerBackend::scanFolder(const QUrl &folderUrl) {
    const QString path = folderUrl.toLocalFile();
    if (path.isEmpty()) return;
    m_scanCoordinator->startScan(path);
}

QString PlayerBackend::combinedFilter() const {
    if (m_categoryFilter.isEmpty()) return m_searchFilter;
    if (m_searchFilter.isEmpty())   return m_categoryFilter;
    return QStringLiteral("(%1) AND (%2)").arg(m_categoryFilter, m_searchFilter);
}

void PlayerBackend::applyFilter() {
    m_trackModel->setFilter(combinedFilter());
    m_trackModel->setSort(m_trackModel->columnFor(m_sortColumn), m_sortOrder);
    m_trackModel->select();
}

void PlayerBackend::filterByAlbum(const QString &albumName, const QString &artistName) {
    QString       newFilter;
    QString       newSortColumn;
    Qt::SortOrder newSortOrder = Qt::AscendingOrder;
    if (albumName.isEmpty()) {
        newSortColumn = kColumnTitle;
    } else {
        if (artistName.isEmpty()) {
            newFilter = QStringLiteral("album = %1").arg(SqlUtils::quote(albumName));
        } else {
            newFilter = QStringLiteral(
                "album = %1 AND "
                "COALESCE(NULLIF(album_artist, ''), artist) = %2")
                .arg(SqlUtils::quote(albumName), SqlUtils::quote(artistName));
        }
        newSortColumn = kColumnTrackNo;
    }
    if (newFilter == m_categoryFilter && newSortColumn == m_sortColumn &&
        newSortOrder == m_sortOrder) return;
    m_categoryFilter = newFilter;
    m_sortColumn     = newSortColumn;
    m_sortOrder      = newSortOrder;
    applyFilter();
}

void PlayerBackend::filterByArtist(const QString &artistName) {
    QString       newFilter;
    QString       newSortColumn;
    Qt::SortOrder newSortOrder = Qt::AscendingOrder;
    if (artistName.isEmpty()) {
        newSortColumn = kColumnTitle;
    } else {
        const QString norm = MusicLibrary::normalizeArtistName(artistName);
        newFilter = QStringLiteral(
            "id IN (SELECT track_id FROM track_artists ta "
            "JOIN artists a ON a.id = ta.artist_id "
            "WHERE a.name_norm = %1)").arg(SqlUtils::quote(norm));
        newSortColumn = kColumnAlbum;
    }
    if (newFilter == m_categoryFilter && newSortColumn == m_sortColumn &&
        newSortOrder == m_sortOrder) return;
    m_categoryFilter = newFilter;
    m_sortColumn     = newSortColumn;
    m_sortOrder      = newSortOrder;
    applyFilter();
}

void PlayerBackend::searchTracks(const QString &query) {
    QString newFilter;
    if (!query.isEmpty()) {
        newFilter = QStringLiteral("search_text LIKE %1 ESCAPE '\\'")
            .arg(SqlUtils::containsPattern(query.toLower()));
    }
    if (newFilter == m_searchFilter) return;
    m_searchFilter = newFilter;
    applyFilter();
}

void PlayerBackend::searchAlbums(const QString &query) {
    m_albumModel->setSearch(query);
}

void PlayerBackend::searchArtists(const QString &query) {
    m_artistModel->setSearch(query);
}

void PlayerBackend::sortTracks(const QString &columnName, bool ascending) {
    if (!isValidSortColumn(columnName)) return;
    m_sortColumn = columnName;
    m_sortOrder  = ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_trackModel->setSort(m_trackModel->columnFor(m_sortColumn), m_sortOrder);
    m_trackModel->select();
}

int PlayerBackend::getRowForPath(const QString &path) {
    if (path.isEmpty()) return -1;

    const QString colName = m_sortColumn.isEmpty() ? QStringLiteral("id") : m_sortColumn;
    const QString order   = m_sortOrder == Qt::AscendingOrder
                              ? QStringLiteral("ASC") : QStringLiteral("DESC");
    const QString filter  = combinedFilter();

    QString sql = QStringLiteral(
        "WITH ranked AS (SELECT path, "
        "ROW_NUMBER() OVER (ORDER BY %1 %2, id ASC) - 1 AS rn "
        "FROM tracks").arg(colName, order);
    if (!filter.isEmpty()) sql += QStringLiteral(" WHERE ") + filter;
    sql += QStringLiteral(") SELECT rn FROM ranked WHERE path = ?");

    QSqlQuery q;
    q.prepare(sql);
    q.addBindValue(path);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toInt();
}

void PlayerBackend::addPlayNext(const QString &path) {
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT path, title, artist, album, duration, tech_info, track_no, album_artist "
        "FROM tracks WHERE path = ?"));
    q.addBindValue(path);
    if (!q.exec() || !q.next()) return;
    m_queueModel->insertTrack(trackFromQuery(q));
}

QVariantMap PlayerBackend::trackContextForPath(const QString &path) {
    QVariantMap result;
    if (path.isEmpty()) return result;

    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT album, "
        "COALESCE(NULLIF(album_artist, ''), artist) AS album_artist, "
        "artist "
        "FROM tracks WHERE path = ?"));
    q.addBindValue(path);
    if (!q.exec() || !q.next()) return result;

    result[QStringLiteral("album")]       = q.value(0).toString();
    result[QStringLiteral("albumArtist")] = q.value(1).toString();
    result[QStringLiteral("artist")]      = q.value(2).toString();
    return result;
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

void PlayerBackend::refreshAllModels() {
    m_trackModel->select();
    m_albumModel->refresh();
    m_artistModel->refresh();
}

void PlayerBackend::resetPlaybackState() {
    if (m_mpv) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command(m_mpv, cmd);
    }
    m_currentTrack = Track();
    m_currentPath.clear();
    m_currentTitle  = kDefaultTitle;
    m_currentArtist = kDefaultArtist;
    m_currentAlbum  = kDefaultAlbum;
    m_currentTechInfo.clear();
    m_currentCoverPath.clear();
    emit metadataChanged();
    emit currentIndexChanged();
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
        del.prepare(QStringLiteral(
            "DELETE FROM tracks WHERE path = ? OR path LIKE ? ESCAPE '\\'"));
        del.addBindValue(clean);
        del.addBindValue(SqlUtils::prefixPattern(clean + QLatin1Char('/')));
        del.exec();
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

    m_libraryWatcher->rescanAll();
}
