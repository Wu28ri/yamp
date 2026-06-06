#include "PlayerBackend.h"

#include "CoverExtractor.h"
#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "ScanSession.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include <clocale>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

#include <mpv/client.h>

#include <alsa/asoundlib.h>

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
    m_sortColumn = TrackModel::TitleColumn;
    m_sortOrder  = Qt::AscendingOrder;
    m_trackModel->setSort(m_sortColumn, m_sortOrder);
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

    initMpv();

    m_paVolume = new PaVolumeController(this);
    connect(m_paVolume, &PaVolumeController::volumeChanged,
            this, [this]() { if (!m_softwareVolume) emit volumeChanged(); });
    connect(m_paVolume, &PaVolumeController::mutedChanged,
            this, [this]() { if (!m_softwareVolume) emit mutedChanged(); });

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
        m_queue.setTracks(queryTracks());
        m_queueModel->resetAll();
        emit currentQueuePositionChanged();
    });
}

PlayerBackend::~PlayerBackend() {
    m_coverPool.waitForDone();
    shutdownMpv();
}

void PlayerBackend::initMpv() {
    setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        qFatal("[mpv] mpv_create failed");
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

    if (!m_audioDevice.isEmpty()) {
        const QString eff = (m_bitPerfectEnabled && m_audioDevice != QStringLiteral("auto"))
            ? m_audioDevice
            : QStringLiteral("auto");
        mpv_set_option_string(m_mpv, "audio-device", eff.toUtf8().constData());
    }

    if (mpv_initialize(m_mpv) < 0) {
        qFatal("[mpv] mpv_initialize failed");
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
    const QByteArray name = QByteArray::fromRawData(prop->name, qstrlen(prop->name));

    if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
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
    } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
        const double d = *static_cast<double*>(prop->data);
        const qint64 ms = static_cast<qint64>(d * 1000.0);
        if (ms != m_durationMs) {
            m_durationMs = ms;
            emit durationChanged();
        }
    } else if (name == "idle-active" && prop->format == MPV_FORMAT_FLAG) {
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

void PlayerBackend::initDatabase() {
    MusicLibrary::initialize();
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
    const bool sw = m_bitPerfectEnabled && m_softwareVolume;
    if (sw) {
        if (m_swMuted == muted) return;
        m_swMuted = muted;
        applyMpvVolume();
        emit mutedChanged();
        return;
    }
    if (m_bitPerfectEnabled) return; // hardware locked, no-op
    m_paVolume->setMuted(muted);
}

void PlayerBackend::setVolume(qreal v) {
    v = qBound(0.0, v, 1.0);
    const bool sw = m_bitPerfectEnabled && m_softwareVolume;
    if (sw) {
        if (qFuzzyCompare(m_swVolume + 1.0, v + 1.0)) return;
        m_swVolume = v;
        applyMpvVolume();
        emit volumeChanged();
        return;
    }
    if (m_bitPerfectEnabled) return; // hardware locked, no-op
    m_paVolume->setVolume(v);
}

bool PlayerBackend::isMuted() const {
    if (m_bitPerfectEnabled) return m_softwareVolume ? m_swMuted : false;
    if (!m_paVolume) return false;
    return m_paVolume->isMuted();
}

qreal PlayerBackend::volume() const {
    if (m_bitPerfectEnabled) return m_softwareVolume ? m_swVolume : 1.0;
    if (!m_paVolume) return m_swVolume;
    return m_paVolume->volume();
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
        // mirror PA state into the cached SW value so the slider doesn't jump
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
    const QString eff = (m_bitPerfectEnabled && !m_audioDevice.isEmpty() && m_audioDevice != QStringLiteral("auto"))
        ? m_audioDevice
        : QStringLiteral("auto");
    mpv_set_property_string(m_mpv, "audio-device", eff.toUtf8().constData());
}

void PlayerBackend::applyMpvVolume() {
    if (!m_mpv) return;
    const bool sw = m_bitPerfectEnabled && m_softwareVolume;
    double vol = sw ? (m_swVolume * 100.0) : 100.0;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    int mute = (sw && m_swMuted) ? 1 : 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &mute);
}

QVariantList PlayerBackend::listHardwareDevices() {
    QVariantList out;

    QHash<int, QPair<QString, QString>> cards; // n -> {short_id, descriptive_name}
    {
        QFile f(QStringLiteral("/proc/asound/cards"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
        const QString text = QString::fromUtf8(f.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        static const QRegularExpression headRe(QStringLiteral(R"(^\s*(\d+)\s+\[([^\]]+)\]:\s*(.*)$)"));
        for (const QString &line : lines) {
            const auto m = headRe.match(line);
            if (!m.hasMatch()) continue;
            const int n = m.captured(1).toInt();
            const QString shortId = m.captured(2).trimmed();
            QString descriptive = m.captured(3).trimmed();
            // Strip leading driver prefix like "USB-Audio - " or "HDA-Intel - "
            const int dashIdx = descriptive.indexOf(QStringLiteral(" - "));
            if (dashIdx > 0 && dashIdx < 24) {
                descriptive = descriptive.mid(dashIdx + 3).trimmed();
            }
            if (descriptive.isEmpty()) descriptive = shortId;
            cards.insert(n, qMakePair(shortId, descriptive));
        }
    }

    QFile pcm(QStringLiteral("/proc/asound/pcm"));
    if (!pcm.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QStringList pcmLines = QString::fromUtf8(pcm.readAll()).split(QLatin1Char('\n'));

    // First pass: count playback devices per card.
    QHash<int, int> playbackCount;
    struct Entry { int card; int dev; QString pcmName; };
    QList<Entry> entries;

    for (const QString &raw : pcmLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 2) continue;

        const QStringList head = parts.at(0).split(QLatin1Char('-'));
        if (head.size() != 2) continue;
        bool okC=false, okD=false;
        const int card = head.at(0).toInt(&okC);
        const int dev  = head.at(1).toInt(&okD);
        if (!okC || !okD) continue;

        bool hasPlayback = false;
        for (int i = 1; i < parts.size(); ++i) {
            if (parts.at(i).trimmed().startsWith(QStringLiteral("playback"))) {
                hasPlayback = true;
                break;
            }
        }
        if (!hasPlayback) continue;
        if (!cards.contains(card)) continue;

        QString pcmName = parts.value(2).trimmed();
        if (pcmName.isEmpty()) pcmName = parts.value(1).trimmed();

        playbackCount[card]++;
        entries.append({card, dev, pcmName});
    }

    for (const Entry &e : entries) {
        const QString cardName = cards.value(e.card).second;
        const bool multi = playbackCount.value(e.card) > 1;

        QVariantMap entry;
        entry.insert(QStringLiteral("name"),
                     QStringLiteral("alsa/hw:CARD=%1,DEV=%2")
                         .arg(cards.value(e.card).first).arg(e.dev));
        if (multi && !e.pcmName.isEmpty() && e.pcmName != cardName) {
            entry.insert(QStringLiteral("description"),
                         QStringLiteral("%1 — %2  (hw:%3,%4)")
                             .arg(cardName, e.pcmName).arg(e.card).arg(e.dev));
        } else {
            entry.insert(QStringLiteral("description"),
                         QStringLiteral("%1  (hw:%2,%3)")
                             .arg(cardName).arg(e.card).arg(e.dev));
        }
        out.append(entry);
    }

    return out;
}

bool PlayerBackend::lockDeviceToZeroDb() {
    if (!m_audioDevice.startsWith(QStringLiteral("alsa/hw:CARD="))) return false;

    // extract card id from "alsa/hw:CARD=<id>,DEV=<n>"
    const QString tail = m_audioDevice.mid(QStringLiteral("alsa/hw:").size()); // "CARD=Pro,DEV=0"
    QString cardId;
    for (const QString &part : tail.split(QLatin1Char(','))) {
        if (part.startsWith(QStringLiteral("CARD="))) {
            cardId = part.mid(5);
            break;
        }
    }
    if (cardId.isEmpty()) return false;

    snd_mixer_t *mixer = nullptr;
    if (snd_mixer_open(&mixer, 0) < 0) return false;

    const QByteArray ctlName = ("hw:CARD=" + cardId).toUtf8();
    bool ok = false;
    do {
        if (snd_mixer_attach(mixer, ctlName.constData()) < 0) break;
        if (snd_mixer_selem_register(mixer, nullptr, nullptr) < 0) break;
        if (snd_mixer_load(mixer) < 0) break;

        int touched = 0;
        for (snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer);
             elem;
             elem = snd_mixer_elem_next(elem)) {
            if (snd_mixer_elem_get_type(elem) != SND_MIXER_ELEM_SIMPLE) continue;
            if (!snd_mixer_selem_is_active(elem)) continue;

            if (snd_mixer_selem_has_playback_volume(elem)) {
                long minDb = 0, maxDb = 0;
                if (snd_mixer_selem_get_playback_dB_range(elem, &minDb, &maxDb) == 0) {
                    snd_mixer_selem_set_playback_dB_all(elem, maxDb, 0);
                } else {
                    long minV = 0, maxV = 0;
                    if (snd_mixer_selem_get_playback_volume_range(elem, &minV, &maxV) == 0) {
                        snd_mixer_selem_set_playback_volume_all(elem, maxV);
                    }
                }
                ++touched;
            }
            if (snd_mixer_selem_has_playback_switch(elem)) {
                snd_mixer_selem_set_playback_switch_all(elem, 1);
            }
        }
        ok = touched > 0;
    } while (false);

    snd_mixer_close(mixer);
    return ok;
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
    if (mode != RgModeTrack && mode != RgModeAlbum) return;
    if (m_rgMode == mode) return;
    m_rgMode = mode;
    applyReplayGainSettings();
}

void PlayerBackend::setReplayGainPreampDb(qreal db) {
    if (db < -15.0) db = -15.0;
    if (db >  15.0) db =  15.0;
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
    if (m_rgEnabled) mode = (m_rgMode == RgModeAlbum) ? "album" : "track";
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
        "SELECT path, title, artist, album, duration, tech_info, track_no FROM tracks");
    if (!whereClause.isEmpty()) sql += QStringLiteral(" WHERE ") + whereClause;
    if (!orderBy.isEmpty()) sql += QStringLiteral(" ORDER BY ") + orderBy;
    else                    sql += QStringLiteral(" ORDER BY id");

    QSqlQuery q;
    q.setForwardOnly(true);
    if (!q.exec(sql)) return out;
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
    m_queue.setTracks(queryTracks(m_categoryFilter, orderBy));
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

    m_currentTrack     = t;
    m_currentIndex     = m_queue.currentGlobalId();
    m_currentPath      = t.path;
    m_currentTitle     = t.title;
    m_currentArtist    = t.artist;
    m_currentAlbum     = t.album;
    m_currentTechInfo  = t.techInfo;
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

    const QString trackPath = t.path;
    const quint64 gen = ++m_coverGen;
    m_coverPool.start([this, trackPath, gen]() {
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

    if (m_libraryWatcher) m_libraryWatcher->registerScannedRoot(path);

    connect(session, &ScanSession::finished, this,
            [this, session](const QString &, const QList<Track> &newTracks) {
                if (m_scanRefreshTimer->isActive()) m_scanRefreshTimer->stop();
                m_queueModel->appendTracks(newTracks);
                refreshAllModels();
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
    QString       newFilter;
    int           newSortColumn;
    Qt::SortOrder newSortOrder = Qt::AscendingOrder;
    if (albumName.isEmpty()) {
        newSortColumn = TrackModel::TitleColumn;
    } else {
        if (artistName.isEmpty()) {
            newFilter = QStringLiteral("album = %1").arg(sqlQuote(albumName));
        } else {
            newFilter = QStringLiteral(
                "album = %1 AND "
                "COALESCE(NULLIF(album_artist, ''), artist) = %2")
                .arg(sqlQuote(albumName), sqlQuote(artistName));
        }
        newSortColumn = TrackModel::TrackNoColumn;
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
    int           newSortColumn;
    Qt::SortOrder newSortOrder = Qt::AscendingOrder;
    if (artistName.isEmpty()) {
        newSortColumn = TrackModel::TitleColumn;
    } else {
        const QString norm = MusicLibrary::normalizeArtistName(artistName);
        newFilter = QStringLiteral(
            "id IN (SELECT track_id FROM track_artists ta "
            "JOIN artists a ON a.id = ta.artist_id "
            "WHERE a.name_norm = %1)").arg(sqlQuote(norm));
        newSortColumn = TrackModel::AlbumColumn;
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
        const QString safe = sqlLikeEscape(query.toLower());
        newFilter = QStringLiteral("search_text LIKE '%%%1%%' ESCAPE '\\'").arg(safe);
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
    if (!q.exec()) return -1;
    if (!q.next()) return -1;
    return q.value(0).toInt();
}

void PlayerBackend::addPlayNext(const QString &path) {
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT title, artist, album, duration, tech_info, track_no "
        "FROM tracks WHERE path = ?"));
    q.addBindValue(path);

    if (!q.exec() || !q.next()) return;
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
    if (m_mpv) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command(m_mpv, cmd);
    }
    m_currentTrack = Track();
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

