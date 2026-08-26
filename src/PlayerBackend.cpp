#include "PlayerBackend.h"

#include "AlsaDevicesService.h"
#include "CoverCacheService.h"
#include "LibraryDb.h"
#include "LibraryWatcher.h"
#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "PaVolumeController.h"
#include "ScanCoordinator.h"
#include "SqlUtils.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>
#include <QTimer>
#include <QUrl>

#include <clocale>
#include <cstring>
#include <utility>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

#include <mpv/client.h>

#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/tpropertymap.h>

#include <algorithm>

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

struct LyricsData {
    QVariantList lines;
    QList<qint64> times;
    QList<int> lineIndices;
    bool synchronized = false;
};

LyricsData parseLyricsText(QString text) {
    LyricsData result;
    text.remove(QChar::ByteOrderMark);

    static const QRegularExpression timestampRe(
        QStringLiteral(R"(\[(\d{1,3}):(\d{1,2})(?:[\.:](\d{1,3}))?\])"));
    static const QRegularExpression enhancedTimestampRe(
        QStringLiteral(R"(<\d{1,3}:\d{1,2}(?:[\.:]\d{1,3})?>)"));
    static const QRegularExpression metadataRe(
        QStringLiteral(R"(^\s*\[(ar|al|ti|by|re|ve|length):.*\]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression offsetRe(
        QStringLiteral(R"(^\s*\[offset:([+-]?\d+)\]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    qint64 offsetMs = 0;
    const QStringList sourceLines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                                Qt::SkipEmptyParts);
    for (const QString &line : sourceLines) {
        const auto match = offsetRe.match(line);
        if (match.hasMatch()) offsetMs = match.captured(1).toLongLong();
    }

    QList<QPair<qint64, QString>> timedLines;
    QStringList plainLines;
    for (QString line : sourceLines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || metadataRe.match(trimmed).hasMatch() ||
            offsetRe.match(trimmed).hasMatch()) {
            continue;
        }

        auto matches = timestampRe.globalMatch(line);
        QList<qint64> lineTimes;
        while (matches.hasNext()) {
            const auto match = matches.next();
            const qint64 minutes = match.captured(1).toLongLong();
            const qint64 seconds = match.captured(2).toLongLong();
            const QString fractionText = match.captured(3);
            qint64 fractionMs = fractionText.toLongLong();
            if (fractionText.size() == 1) fractionMs *= 100;
            else if (fractionText.size() == 2) fractionMs *= 10;
            lineTimes.append(qMax<qint64>(0, minutes * 60000 + seconds * 1000 +
                                             fractionMs + offsetMs));
        }

        if (lineTimes.isEmpty()) {
            plainLines.append(trimmed);
            continue;
        }

        line.remove(timestampRe);
        line.remove(enhancedTimestampRe);
        line = line.trimmed();
        for (qint64 timeMs : lineTimes) timedLines.append({timeMs, line});
    }

    if (!timedLines.isEmpty()) {
        std::stable_sort(timedLines.begin(), timedLines.end(),
                         [](const auto &left, const auto &right) {
                             return left.first < right.first;
                         });
        result.synchronized = true;
        for (const auto &[timeMs, line] : timedLines) {
            result.times.append(timeMs);
            if (line.isEmpty()) {
                result.lineIndices.append(-1);
            } else {
                result.lineIndices.append(result.lines.size());
                result.lines.append(QVariantMap{{QStringLiteral("timeMs"), timeMs},
                                                {QStringLiteral("text"), line}});
            }
        }
        return result;
    }

    for (const QString &line : plainLines) {
        result.lines.append(QVariantMap{{QStringLiteral("timeMs"), -1},
                                        {QStringLiteral("text"), line}});
    }
    return result;
}

LyricsData readLyrics(const QString &trackPath) {
    if (trackPath.isEmpty()) return {};

    const QFileInfo trackInfo(trackPath);
    const QString sidecarPath = trackInfo.dir().filePath(trackInfo.completeBaseName() +
                                                         QStringLiteral(".lrc"));
    QFile sidecar(sidecarPath);
    if (sidecar.open(QIODevice::ReadOnly)) {
        LyricsData data = parseLyricsText(QString::fromUtf8(sidecar.readAll()));
        if (!data.lines.isEmpty()) return data;
    }

    const QByteArray pathBytes = trackPath.toUtf8();
    TagLib::FileRef fileRef(pathBytes.constData(), false);
    if (fileRef.isNull() || !fileRef.file()) return {};

    if (auto *mpegFile = dynamic_cast<TagLib::MPEG::File *>(fileRef.file())) {
        if (mpegFile->hasID3v2Tag()) {
            const auto frames = mpegFile->ID3v2Tag()->frameList("SYLT");
            for (auto *frame : frames) {
                auto *lyrics = dynamic_cast<TagLib::ID3v2::SynchronizedLyricsFrame *>(frame);
                if (!lyrics || lyrics->timestampFormat() !=
                                   TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds ||
                    lyrics->type() != TagLib::ID3v2::SynchronizedLyricsFrame::Lyrics) {
                    continue;
                }

                LyricsData data;
                QList<QPair<qint64, QString>> timedLines;
                for (const auto &entry : lyrics->synchedText()) {
                    const QString line = QString::fromStdString(entry.text.to8Bit(true)).trimmed();
                    timedLines.append({entry.time, line});
                }
                if (!timedLines.isEmpty()) {
                    std::stable_sort(timedLines.begin(), timedLines.end(),
                                     [](const auto &left, const auto &right) {
                                         return left.first < right.first;
                    });
                    for (const auto &[timeMs, line] : timedLines) {
                        data.times.append(timeMs);
                        if (line.isEmpty()) {
                            data.lineIndices.append(-1);
                        } else {
                            data.lineIndices.append(data.lines.size());
                            data.lines.append(QVariantMap{{QStringLiteral("timeMs"), timeMs},
                                                          {QStringLiteral("text"), line}});
                        }
                    }
                    data.synchronized = true;
                    return data;
                }
            }
        }
    }

    const TagLib::PropertyMap properties = fileRef.file()->properties();
    QString lyricsText;
    const auto appendValues = [&lyricsText](const TagLib::StringList &values) {
        for (const auto &value : values) {
            const QString text = QString::fromStdString(value.to8Bit(true)).trimmed();
            if (!text.isEmpty()) {
                if (!lyricsText.isEmpty()) lyricsText.append(QLatin1Char('\n'));
                lyricsText.append(text);
            }
        }
    };

    if (const auto it = properties.find("LYRICS"); it != properties.end()) {
        appendValues(it->second);
    }
    if (lyricsText.isEmpty()) {
        for (const auto &[key, values] : properties) {
            const QString keyText = QString::fromStdString(key.to8Bit(true));
            if (keyText.startsWith(QStringLiteral("LYRICS:"), Qt::CaseInsensitive) ||
                keyText.compare(QStringLiteral("UNSYNCEDLYRICS"), Qt::CaseInsensitive) == 0) {
                appendValues(values);
                if (!lyricsText.isEmpty()) break;
            }
        }
    }

    return parseLyricsText(lyricsText);
}
}

PlayerBackend::PlayerBackend(QObject *parent)
    : QObject(parent),
      m_currentTitle(kDefaultTitle),
      m_currentArtist(kDefaultArtist),
      m_currentAlbum(kDefaultAlbum) {

    if (!MusicLibrary::initialize())
        qFatal("[PlayerBackend] music library database initialization failed");

    m_trackModel = new TrackModel(this);
    m_trackModel->setTable(QStringLiteral("tracks"));
    m_trackModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_trackModel->setSort(m_trackModel->columnFor(m_sortColumn), m_sortOrder);
    m_trackModel->select();

    m_albumModel = new AlbumModel(this);
    m_albumModel->reload();

    m_artistModel = new ArtistModel(this);
    m_artistModel->reload();

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
            [this](const QList<Track> &) {
                m_scanRefreshTimer->stop();
                refreshAllModels();
                if (m_queue.count() == 0) rebuildQueueFromCurrentFilter();
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
    connect(m_paVolume, &PaVolumeController::hardwareDeviceExclusiveChanged,
            this, [this](bool exclusive, bool success) {
        if (!m_audioExclusiveTransition) return;
        if (exclusive && !success) {
            if (m_softwareVolume && m_paVolume) {
                m_paVolume->setVolume(m_swVolume);
                m_paVolume->setMuted(m_swMuted);
            }
            m_bitPerfectEnabled = false;
            m_audioExclusiveHeld = false;
            applyAudioDeviceToMpv();
            applyMpvVolume();
            applyReplayGainSettings();
            emit bitPerfectChanged();
            emit volumeControllableChanged();
            emit volumeChanged();
            emit mutedChanged();
            if (m_paVolume)
                m_paVolume->setHardwareDeviceExclusive(m_audioDevice, false);
            return;
        }
        if (exclusive) {
            m_audioExclusiveHeld = true;
            m_audioExclusiveTransition = false;
            if (!m_pendingAudioDevice.isEmpty()) {
                m_reenableBitPerfectAfterRestore = true;
                releaseExclusiveDevice();
                return;
            }
            if (!m_pendingExclusiveTrack.isValid() && !m_resumeAfterExclusive &&
                (!m_hasFile || m_paused)) {
                releaseExclusiveDevice();
                return;
            }
            QTimer::singleShot(150, this, [this]() {
                if (m_bitPerfectEnabled && m_audioExclusiveHeld &&
                    !m_audioExclusiveTransition) {
                    applyAudioDeviceToMpv();
                    continuePendingPlayback();
                    if (!m_softwareVolume) m_hardwareVolumeTimer->start();
                }
            });
            return;
        }
        if (!success) {
            if (++m_audioRestoreRetries > 8) {
                m_audioExclusiveHeld = false;
                m_audioExclusiveTransition = false;
                if (m_reenableBitPerfectAfterRestore) {
                    m_reenableBitPerfectAfterRestore = false;
                    m_audioDevice = std::exchange(m_pendingAudioDevice, {});
                    refreshHardwareVolume();
                    emit audioDeviceChanged();
                }
                continuePendingPlayback();
                return;
            }
            QTimer::singleShot(250, this, [this]() {
                if (m_audioExclusiveTransition && m_paVolume)
                    m_paVolume->setHardwareDeviceExclusive(m_audioDevice, false);
            });
            return;
        }

        m_audioExclusiveHeld = false;
        m_audioExclusiveTransition = false;
        m_audioRestoreRetries = 0;
        if (m_reenableBitPerfectAfterRestore) {
            m_reenableBitPerfectAfterRestore = false;
            m_audioDevice = std::exchange(m_pendingAudioDevice, {});
            refreshHardwareVolume();
            emit audioDeviceChanged();
        }
        if (m_bitPerfectEnabled &&
            (m_pendingExclusiveTrack.isValid() || m_resumeAfterExclusive ||
             (m_hasFile && !m_paused))) {
            requestExclusiveForPlayback();
        } else {
            if (m_hardwareVolumeAvailable && !m_softwareVolume && m_paVolume) {
                const qreal volume = m_hardwareVolume;
                const bool muted = m_hardwareMuted;
                const QString device = m_audioDevice;
                QTimer::singleShot(50, this, [this, device, volume, muted]() {
                    if (m_paVolume)
                        m_paVolume->setHardwareDeviceVolume(device, volume, muted);
                });
                QTimer::singleShot(250, this, [this]() {
                    if (m_bitPerfectEnabled && !m_softwareVolume) {
                        refreshHardwareVolume();
                        m_hardwareVolumeTimer->start();
                    }
                });
            }
            if (!m_bitPerfectEnabled) continuePendingPlayback();
        }
    });

    m_hardwareVolumeTimer = new QTimer(this);
    m_hardwareVolumeTimer->setInterval(500);
    connect(m_hardwareVolumeTimer, &QTimer::timeout,
            this, &PlayerBackend::refreshHardwareVolume);

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
                updateCurrentLyricIndex(ms);
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
        case MPV_EVENT_START_FILE: {
            auto *start = static_cast<mpv_event_start_file*>(e->data);
            m_loadingMpvEntryId = start ? start->playlist_entry_id : -1;
            break;
        }
        case MPV_EVENT_FILE_LOADED: {
            const auto it = m_mpvEntryTracks.constFind(m_loadingMpvEntryId);
            if (m_loadingMpvEntryId == m_currentMpvEntryId &&
                it != m_mpvEntryTracks.constEnd() && it->path == m_currentPath)
                emit trackStarted(it.value());
            break;
        }
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
                if (m_bitPerfectEnabled) releaseExclusiveDevice();
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
    m_mpvEntryTracks.remove(ev->playlist_entry_id);
    if (m_loadingMpvEntryId == ev->playlist_entry_id) m_loadingMpvEntryId = -1;
    if (ev->playlist_entry_id != m_currentMpvEntryId) return;
    m_currentMpvEntryId = -1;
    switch (ev->reason) {
    case MPV_END_FILE_REASON_EOF:
        playNext();
        break;
    case MPV_END_FILE_REASON_ERROR:
        qWarning("[mpv] playback failed: %s", mpv_error_string(ev->error));
        if (m_bitPerfectEnabled && ev->error == MPV_ERROR_AO_INIT_FAILED)
            setBitPerfect(false);
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
    if (usingHardwareVolume()) {
        if (AlsaDevices::setHardwareMuted(m_audioDevice, muted)) refreshHardwareVolume();
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
    if (usingHardwareVolume()) {
        if (AlsaDevices::setHardwareVolume(m_audioDevice, v)) refreshHardwareVolume();
        return;
    }
    if (m_bitPerfectEnabled) return;
    m_paVolume->setVolume(v);
}

bool PlayerBackend::isMuted() const {
    if (usingSoftwareVolume()) return m_swMuted;
    if (usingHardwareVolume()) return m_hardwareMuted;
    if (m_bitPerfectEnabled) return false;
    return m_paVolume ? m_paVolume->isMuted() : false;
}

qreal PlayerBackend::volume() const {
    if (usingSoftwareVolume()) return m_swVolume;
    if (usingHardwareVolume()) return m_hardwareVolume;
    if (m_bitPerfectEnabled) return 1.0;
    return m_paVolume ? m_paVolume->volume() : m_swVolume;
}

void PlayerBackend::setBitPerfect(bool enabled) {
    if (m_audioExclusiveTransition) {
        emit bitPerfectChanged();
        return;
    }
    if (m_bitPerfectEnabled == enabled) return;
    if (enabled) {
        const QVariantList devices = AlsaDevices::list();
        if (devices.isEmpty()) {
            qWarning("[mpv] bit-perfect output requires a hardware ALSA device");
            emit bitPerfectChanged();
            return;
        }
        bool deviceAvailable = false;
        for (const QVariant &device : devices) {
            if (device.toMap().value(QStringLiteral("name")).toString() == m_audioDevice) {
                deviceAvailable = true;
                break;
            }
        }
        if (!deviceAvailable) {
            m_audioDevice = devices.constFirst().toMap().value(QStringLiteral("name")).toString();
            emit audioDeviceChanged();
        }
    }
    if (!enabled && m_softwareVolume && m_paVolume) {
        m_paVolume->setVolume(m_swVolume);
        m_paVolume->setMuted(m_swMuted);
    }
    m_bitPerfectEnabled = enabled;
    if (enabled && m_paVolume) {
        m_swVolume = m_paVolume->volume();
        m_swMuted  = m_paVolume->isMuted();
    }
    if (enabled) {
        refreshHardwareVolume();
        if (!m_softwareVolume) m_hardwareVolumeTimer->start();
    } else {
        m_hardwareVolumeTimer->stop();
    }
    if (enabled && m_hasFile && !m_paused) requestExclusiveForPlayback();
    if (!enabled && m_audioExclusiveHeld) releaseExclusiveDevice();
    if (!enabled && !m_audioExclusiveHeld) applyAudioDeviceToMpv();
    if (m_mpv)
        mpv_set_property_string(m_mpv, "gapless-audio", enabled ? "no" : "weak");
    applyMpvVolume();
    applyReplayGainSettings();
    emit bitPerfectChanged();
    emit volumeControllableChanged();
    emit volumeChanged();
    emit mutedChanged();
}

void PlayerBackend::setAudioDevice(const QString &device) {
    const QString normalized = device.isEmpty() ? QStringLiteral("auto") : device;
    if (m_audioExclusiveTransition) {
        m_pendingAudioDevice = normalized;
        m_reenableBitPerfectAfterRestore = true;
        return;
    }
    if (m_audioDevice == normalized) return;
    if (m_bitPerfectEnabled && m_audioExclusiveHeld) {
        m_pendingAudioDevice = normalized;
        m_reenableBitPerfectAfterRestore = true;
        releaseExclusiveDevice();
        return;
    }
    m_audioDevice = normalized;
    refreshHardwareVolume();
    emit audioDeviceChanged();
}

void PlayerBackend::setSoftwareVolume(bool enabled) {
    if (m_softwareVolume == enabled) return;
    m_softwareVolume = enabled;
    if (enabled && m_paVolume) {
        m_swVolume = m_paVolume->volume();
        m_swMuted  = m_paVolume->isMuted();
    }
    applyMpvVolume();
    if (m_bitPerfectEnabled) {
        if (enabled) {
            m_hardwareVolumeTimer->stop();
        } else {
            refreshHardwareVolume();
            m_hardwareVolumeTimer->start();
        }
    }
    emit softwareVolumeChanged();
    emit volumeControllableChanged();
    emit volumeChanged();
    emit mutedChanged();
}

void PlayerBackend::applyAudioDeviceToMpv() {
    if (!m_mpv) return;
    const QString eff = (m_bitPerfectEnabled && m_audioExclusiveHeld &&
                         m_audioDevice != QStringLiteral("auto"))
        ? m_audioDevice
        : QStringLiteral("auto");
    const QByteArray device = eff.toUtf8();
    const int result = mpv_set_property_string(m_mpv, "audio-device", device.constData());
    if (result < 0) {
        qWarning("[mpv] could not select audio device %s: %s",
                 device.constData(), mpv_error_string(result));
    }
}

void PlayerBackend::requestExclusiveForPlayback() {
    if (!m_bitPerfectEnabled || m_audioExclusiveHeld || m_audioExclusiveTransition)
        return;
    m_audioExclusiveTransition = true;
    m_hardwareVolumeTimer->stop();
    if (m_paVolume) m_paVolume->setHardwareDeviceExclusive(m_audioDevice, true);
}

void PlayerBackend::releaseExclusiveDevice() {
    if (!m_audioExclusiveHeld || m_audioExclusiveTransition) return;
    m_audioExclusiveHeld = false;
    m_hardwareVolumeTimer->stop();
    m_audioExclusiveTransition = true;
    m_audioRestoreRetries = 0;
    applyAudioDeviceToMpv();
    if (m_paVolume) m_paVolume->setHardwareDeviceExclusive(m_audioDevice, false);
}

void PlayerBackend::continuePendingPlayback() {
    const Track pending = std::exchange(m_pendingExclusiveTrack, {});
    const bool resume = std::exchange(m_resumeAfterExclusive, false);
    if (pending.isValid()) {
        loadTrackIntoMpv(pending);
    } else if (resume && m_mpv && m_hasFile) {
        int paused = 0;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    }
}

void PlayerBackend::refreshHardwareVolume() {
    if (m_audioExclusiveTransition) return;
    qreal volume = 1.0;
    bool muted = false;
    const bool available = AlsaDevices::hardwareVolume(m_audioDevice, volume, muted);
    const bool availabilityChanged = available != m_hardwareVolumeAvailable;
    const bool volumeChanged = available &&
        !qFuzzyCompare(m_hardwareVolume + 1.0, volume + 1.0);
    const bool muteChanged = available && muted != m_hardwareMuted;
    m_hardwareVolumeAvailable = available;
    if (available) {
        m_hardwareVolume = volume;
        m_hardwareMuted = muted;
    }
    if (availabilityChanged) emit volumeControllableChanged();
    if (availabilityChanged || volumeChanged) emit PlayerBackend::volumeChanged();
    if (availabilityChanged || muteChanged) emit mutedChanged();
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
    if (m_rgEnabled)
        mode = (m_rgMode == ReplayGainMode::RgModeAlbum) ? "album" : "track";
    mpv_set_property_string(m_mpv, "replaygain", mode);

    double preamp = m_rgPreampDb;
    mpv_set_property(m_mpv, "replaygain-preamp", MPV_FORMAT_DOUBLE, &preamp);

    mpv_set_property_string(m_mpv, "replaygain-clip", m_rgClipProtect ? "no" : "yes");
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
    double t = static_cast<double>(ms) / 1000.0;
    if (mpv_set_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &t) >= 0) {
        m_lastPositionMs = ms;
        updateCurrentLyricIndex(ms);
        emit positionChanged();
    }
}

void PlayerBackend::play() {
    if (!m_mpv) return;
    if (!m_hasFile && m_currentTrack.isValid()) {
        loadTrack(m_currentTrack);
        return;
    }
    if (m_bitPerfectEnabled && !m_audioExclusiveHeld) {
        m_resumeAfterExclusive = m_hasFile;
        requestExclusiveForPlayback();
        return;
    }
    int p = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
}

void PlayerBackend::pause() {
    if (!m_mpv) return;
    m_pendingExclusiveTrack = Track();
    m_resumeAfterExclusive = false;
    int p = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &p);
    if (m_bitPerfectEnabled) releaseExclusiveDevice();
}

void PlayerBackend::stop() {
    if (!m_mpv) return;
    m_pendingExclusiveTrack = Track();
    m_resumeAfterExclusive = false;
    const char *cmd[] = {"stop", nullptr};
    mpv_command(m_mpv, cmd);
    if (m_bitPerfectEnabled) releaseExclusiveDevice();
}

void PlayerBackend::togglePlayback() {
    if (!m_mpv) return;
    if (m_hasFile) {
        if (m_paused) play();
        else pause();
        return;
    }
    if (m_currentTrack.isValid()) {
        loadTrack(m_currentTrack);
    } else if (m_queue.count() > 0) {
        playFromQueue(0);
    }
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
    const QString currentPath = m_currentPath;
    QString orderBy;
    if (!m_sortColumn.isEmpty()) {
        orderBy = m_sortColumn + (m_sortOrder == Qt::AscendingOrder
                                  ? QStringLiteral(" ASC") : QStringLiteral(" DESC"));
    }
    m_queue.setTracks(queryTracks(m_categoryFilter, orderBy));
    if (!currentPath.isEmpty() && m_queue.containsPath(currentPath))
        m_queue.setIndexByPath(currentPath);
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
        if (m_queue.isShuffle()) {
            m_queue.setIndexByPath(path);
            m_queueModel->resetAll();
        } else {
            m_queue.jumpToPosition(existing);
        }
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

void PlayerBackend::playNext() {
    const Track next = m_queue.next();
    if (next.isValid()) loadTrack(next);
    else if (m_bitPerfectEnabled) releaseExclusiveDevice();
}
void PlayerBackend::playPrevious() { loadTrack(m_queue.previous()); }

void PlayerBackend::loadTrack(const Track &t) {
    if (!t.isValid()) return;

    pruneQueueToLibrary();
    if (!QFileInfo::exists(t.path)) {
        const int missingPosition = m_queue.positionOfPath(t.path);
        if (missingPosition >= 0) {
            m_queue.removeTrack(missingPosition);
            m_queueModel->resetAll();
        }
        QTimer::singleShot(0, this, &PlayerBackend::playNext);
        return;
    }

    m_currentTrack    = t;
    m_currentPath     = t.path;
    m_currentTitle    = t.title;
    m_currentArtist   = t.artist;
    m_currentAlbum    = t.album;
    m_currentTechInfo = t.techInfo;
    m_currentCoverPath.clear();
    loadLyrics(t.path);

    if (m_bitPerfectEnabled && !m_audioExclusiveHeld) {
        m_pendingExclusiveTrack = t;
        requestExclusiveForPlayback();
    } else {
        loadTrackIntoMpv(t);
    }

    m_queueModel->notifyCurrentChanged();
    emit currentIndexChanged();
    emit currentQueuePositionChanged();
    emit metadataChanged();

    m_coverService->requestCoverFor(t.path, ++m_coverGen);
}

void PlayerBackend::loadTrackIntoMpv(const Track &track) {
    if (!m_mpv || !track.isValid()) return;
    int paused = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    const QByteArray path = track.path.toUtf8();
    const char *command[] = {"loadfile", path.constData(), nullptr};
    if (mpv_command(m_mpv, command) < 0) return;

    int64_t entryId = -1;
    if (mpv_get_property(m_mpv, "playlist/0/id", MPV_FORMAT_INT64, &entryId) >= 0) {
        m_mpvEntryTracks.insert(entryId, track);
        m_currentMpvEntryId = entryId;
    }
}

void PlayerBackend::scanFolder(const QUrl &folderUrl) {
    const QString path = folderUrl.toLocalFile();
    if (path.isEmpty()) return;
    if (m_clearPending) {
        m_deferredScanFolders.insert(QDir(path).absolutePath());
        return;
    }
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
        newFilter = QStringLiteral("%1 LIKE %2 ESCAPE '\\'")
            .arg(SqlUtils::normalizedSearchExpression(QStringLiteral("search_text")),
                 SqlUtils::containsPattern(SqlUtils::normalizeSearch(query)));
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
    if (m_queue.isShuffle()) {
        m_queueBuiltFromSort = m_sortColumn;
        m_queueBuiltFromOrder = m_sortOrder;
        return;
    }
    rebuildQueueFromCurrentFilter();
    emit currentQueuePositionChanged();
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
    m_albumModel->reload();
    m_artistModel->reload();

    pruneQueueToLibrary();
}

void PlayerBackend::pruneQueueToLibrary() {
    QSqlQuery query;
    if (!query.exec(QStringLiteral("SELECT path FROM tracks"))) return;

    QSet<QString> paths;
    while (query.next()) paths.insert(query.value(0).toString());
    if (m_hasFile && !m_currentPath.isEmpty()) paths.insert(m_currentPath);
    if (m_queueModel->retainPaths(paths)) {
        emit currentQueuePositionChanged();
    }
}

void PlayerBackend::resetPlaybackState() {
    const bool stateChanged = m_hasFile || !m_paused;
    m_pendingExclusiveTrack = Track();
    m_resumeAfterExclusive = false;
    if (m_bitPerfectEnabled) releaseExclusiveDevice();
    if (m_mpv) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command(m_mpv, cmd);
    }
    m_currentTrack = Track();
    m_currentMpvEntryId = -1;
    m_currentPath.clear();
    m_currentTitle  = kDefaultTitle;
    m_currentArtist = kDefaultArtist;
    m_currentAlbum  = kDefaultAlbum;
    m_currentTechInfo.clear();
    m_currentCoverPath.clear();
    loadLyrics({});
    m_hasFile = false;
    m_paused = true;
    m_positionPollTimer->stop();
    if (m_durationMs != 0) {
        m_durationMs = 0;
        emit durationChanged();
    }
    if (m_lastPositionMs != 0) {
        m_lastPositionMs = 0;
        emit positionChanged();
    }
    emit metadataChanged();
    emit currentIndexChanged();
    if (stateChanged) emit playbackStateChanged();
}

void PlayerBackend::loadLyrics(const QString &trackPath) {
    const LyricsData data = readLyrics(trackPath);
    m_lyricsLines = data.lines;
    m_lyricTimes = data.times;
    m_lyricLineIndices = data.lineIndices;
    m_lyricsSynchronized = data.synchronized;
    m_currentLyricIndex = -1;
    emit lyricsChanged();
    emit currentLyricIndexChanged();
}

void PlayerBackend::updateCurrentLyricIndex(qint64 positionMs) {
    int index = -1;
    if (m_lyricsSynchronized && !m_lyricTimes.isEmpty()) {
        const auto it = std::upper_bound(m_lyricTimes.cbegin(), m_lyricTimes.cend(), positionMs);
        const int eventIndex = static_cast<int>(std::distance(m_lyricTimes.cbegin(), it)) - 1;
        if (eventIndex >= 0 && eventIndex < m_lyricLineIndices.size())
            index = m_lyricLineIndices.at(eventIndex);
    }
    if (index == m_currentLyricIndex) return;
    m_currentLyricIndex = index;
    emit currentLyricIndexChanged();
}

void PlayerBackend::clearLibrary() {
    if (m_clearPending) return;
    m_clearPending = true;
    m_scanCoordinator->cancelAll();
    resetPlaybackState();

    m_queue.setTracks({});
    m_queueModel->resetAll();

    m_libraryWatcher->clearAll();

    clearLibraryDatabase();
}

void PlayerBackend::clearLibraryDatabase() {
    QSqlDatabase db = QSqlDatabase::database();
    LibraryDb::NonBlockingWrite nonBlocking(db);
    if (!db.transaction()) {
        QTimer::singleShot(250, this, &PlayerBackend::clearLibraryDatabase);
        return;
    }
    QSqlQuery q(db);
    const bool cleared = q.exec(QStringLiteral("DELETE FROM tracks")) &&
                         q.exec(QStringLiteral("DELETE FROM track_artists")) &&
                         q.exec(QStringLiteral("DELETE FROM artists")) &&
                         q.exec(QStringLiteral("DELETE FROM watch_roots"));
    if (!cleared || !db.commit()) {
        db.rollback();
        QTimer::singleShot(250, this, &PlayerBackend::clearLibraryDatabase);
        return;
    }

    m_categoryFilter.clear();
    m_searchFilter.clear();
    m_trackModel->setFilter(QString());
    refreshAllModels();

    m_clearPending = false;
    const QSet<QString> deferred = std::exchange(m_deferredScanFolders, {});
    for (const QString &folder : deferred) scanFolder(QUrl::fromLocalFile(folder));
    emit currentQueuePositionChanged();
}

void PlayerBackend::removeFolder(const QString &folder, const QStringList &remainingFolders) {
    if (folder.isEmpty()) return;
    const QString clean = QDir(folder).absolutePath();
    if (clean.isEmpty()) return;
    m_desiredFolders = remainingFolders;
    if (m_clearPending) {
        m_deferredScanFolders.remove(clean);
        m_scanCoordinator->cancelRoot(clean);
        return;
    }

    m_scanCoordinator->cancelRoot(clean);
    m_libraryWatcher->removeRoot(clean);
    m_libraryWatcher->rescanAll(m_scanCoordinator->activeRoots());

    removeFolderFromDatabase(clean);
}

void PlayerBackend::removeFolderFromDatabase(const QString &clean) {
    const QStringList roots = m_libraryWatcher->roots();
    if (roots.contains(clean)) return;
    bool removeTracks = true;
    for (const QString &root : roots) {
        if (clean.startsWith(root + QLatin1Char('/'))) {
            removeTracks = false;
            break;
        }
    }

    {
        QSqlDatabase db = QSqlDatabase::database();
        LibraryDb::NonBlockingWrite nonBlocking(db);
        if (!db.transaction()) {
            QTimer::singleShot(250, this, [this, clean]() {
                removeFolderFromDatabase(clean);
            });
            return;
        }
        QSqlQuery del(db);
        if (removeTracks) {
            QString sql = QStringLiteral(
                "DELETE FROM tracks WHERE (path = ? OR path LIKE ? ESCAPE '\\')");
            QStringList descendants;
            for (const QString &root : roots) {
                if (root.startsWith(clean + QLatin1Char('/'))) {
                    sql += QStringLiteral(
                        " AND NOT (path = ? OR path LIKE ? ESCAPE '\\')");
                    descendants.append(root);
                }
            }
            del.prepare(sql);
            del.addBindValue(clean);
            del.addBindValue(SqlUtils::prefixPattern(clean + QLatin1Char('/')));
            for (const QString &root : descendants) {
                del.addBindValue(root);
                del.addBindValue(SqlUtils::prefixPattern(root + QLatin1Char('/')));
            }
        }
        QSqlQuery delRoot(db);
        delRoot.prepare(QStringLiteral("DELETE FROM watch_roots WHERE path = ?"));
        delRoot.addBindValue(clean);
        if ((removeTracks && !del.exec()) || !delRoot.exec() || !db.commit()) {
            db.rollback();
            QTimer::singleShot(250, this, [this, clean]() {
                removeFolderFromDatabase(clean);
            });
            return;
        }
        if (removeTracks) MusicLibrary::pruneOrphanArtists(db);
    }

    if (!removeTracks) return;

    const QString prefix = clean + QLatin1Char('/');
    const bool currentInRemoved = !m_currentPath.isEmpty() &&
                                   (m_currentPath == clean || m_currentPath.startsWith(prefix));
    bool currentPreserved = false;
    for (const QString &root : roots) {
        if (m_currentPath == root || m_currentPath.startsWith(root + QLatin1Char('/'))) {
            currentPreserved = true;
            break;
        }
    }
    if (currentInRemoved && !currentPreserved) {
        resetPlaybackState();
    }

    refreshAllModels();

    rebuildQueueFromCurrentFilter();
    emit currentQueuePositionChanged();
    if (!m_desiredFolders.isEmpty()) syncWithFolders(m_desiredFolders);
}

void PlayerBackend::syncWithFolders(const QStringList &folders) {
    m_desiredFolders = folders;
    QSet<QString> desired;
    for (const QString &raw : folders) {
        const QString clean = QDir(raw).absolutePath();
        if (!clean.isEmpty() && QDir(clean).exists()) desired.insert(clean);
    }

    const QStringList currentRoots = m_libraryWatcher->roots();
    const QSet<QString> currentSet(currentRoots.begin(), currentRoots.end());

    for (const QString &existing : currentSet) {
        if (!desired.contains(existing)) removeFolder(existing, folders);
    }
    for (const QString &want : desired) {
        if (!currentSet.contains(want)) scanFolder(QUrl::fromLocalFile(want));
    }
    m_libraryWatcher->rescanAll(m_scanCoordinator->activeRoots());
}
