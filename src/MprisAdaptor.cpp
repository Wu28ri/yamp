#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "PlayerBackend.h"

#include <QUrl>
#include <QTimer>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

namespace {
constexpr const char *kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kPlayerInterface = "org.mpris.MediaPlayer2.Player";
}

MprisRootAdaptor::MprisRootAdaptor(PlayerBackend *backend)
    : QDBusAbstractAdaptor(backend), m_backend(backend) {}

void MprisRootAdaptor::Raise() {
    if (m_backend) m_backend->requestRaise();
}

MprisPlayerAdaptor::MprisPlayerAdaptor(PlayerBackend *backend)
    : QDBusAbstractAdaptor(backend), m_backend(backend) {

    m_lastCanGoNext     = canGoNext();
    m_lastCanGoPrevious = canGoPrevious();
    m_lastCanPlay       = canPlay();
    m_lastCanPause      = canPause();
    m_lastCanSeek       = canSeek();

    m_metadataTimer = new QTimer(this);
    m_metadataTimer->setSingleShot(true);
    m_metadataTimer->setInterval(50);
    connect(m_metadataTimer, &QTimer::timeout, this, [this]() {
        emitProperties({{QStringLiteral("Metadata"), metadata()}});
    });

    connect(m_backend, &PlayerBackend::metadataChanged, this, &MprisPlayerAdaptor::invalidateMetadataCache);
    connect(m_backend, &PlayerBackend::durationChanged, this, &MprisPlayerAdaptor::invalidateMetadataCache);
    connect(m_backend, &PlayerBackend::metadataChanged, this, &MprisPlayerAdaptor::scheduleMetadataPush);
    connect(m_backend, &PlayerBackend::durationChanged, this, &MprisPlayerAdaptor::scheduleMetadataPush);
    connect(m_backend, &PlayerBackend::metadataChanged, this, &MprisPlayerAdaptor::syncCapabilities);
    connect(m_backend, &PlayerBackend::durationChanged, this, &MprisPlayerAdaptor::syncCapabilities);
    connect(m_backend, &PlayerBackend::queueCountChanged, this, &MprisPlayerAdaptor::syncCapabilities);
    connect(m_backend, &PlayerBackend::currentQueuePositionChanged, this, &MprisPlayerAdaptor::syncCapabilities);

    connect(m_backend, &PlayerBackend::playbackStateChanged, this, [this]() {
        emitProperties({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
        scheduleMetadataPush();
        syncCapabilities();
    });

    connect(m_backend, &PlayerBackend::volumeChanged, this, [this]() {
        emitProperties({{QStringLiteral("Volume"), volume()}});
    });

    connect(m_backend, &PlayerBackend::shuffleChanged, this, [this]() {
        emitProperties({{QStringLiteral("Shuffle"), shuffle()}});
        syncCapabilities();
    });

    connect(m_backend, &PlayerBackend::repeatModeChanged, this, [this]() {
        emit loopStatusChanged();
        emitProperties({{QStringLiteral("LoopStatus"), loopStatus()}});
        syncCapabilities();
    });

    connect(m_backend, &PlayerBackend::seeked, this, [this](qint64 positionMs) {
        emit Seeked(static_cast<qlonglong>(positionMs) * 1000);
    });
}

void MprisPlayerAdaptor::syncCapabilities() {
    QVariantMap changed;
    const bool next = canGoNext();
    const bool prev = canGoPrevious();
    const bool play = canPlay();
    const bool pause = canPause();
    const bool seek = canSeek();

    if (next != m_lastCanGoNext) {
        m_lastCanGoNext = next;
        changed.insert(QStringLiteral("CanGoNext"), next);
    }
    if (prev != m_lastCanGoPrevious) {
        m_lastCanGoPrevious = prev;
        changed.insert(QStringLiteral("CanGoPrevious"), prev);
    }
    if (play != m_lastCanPlay) {
        m_lastCanPlay = play;
        changed.insert(QStringLiteral("CanPlay"), play);
    }
    if (pause != m_lastCanPause) {
        m_lastCanPause = pause;
        changed.insert(QStringLiteral("CanPause"), pause);
    }
    if (seek != m_lastCanSeek) {
        m_lastCanSeek = seek;
        changed.insert(QStringLiteral("CanSeek"), seek);
    }
    if (!changed.isEmpty()) {
        emit capabilitiesChanged();
        emitProperties(changed);
    }
}

void MprisPlayerAdaptor::scheduleMetadataPush() {
    if (!m_metadataTimer->isActive()) m_metadataTimer->start();
}

void MprisPlayerAdaptor::invalidateMetadataCache() {
    m_metadataDirty = true;
}

void MprisPlayerAdaptor::emitProperties(const QVariantMap &props) {
    QDBusMessage msg = QDBusMessage::createSignal(
        QString::fromLatin1(kObjectPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    msg << QString::fromLatin1(kPlayerInterface);
    msg << props;
    msg << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

QString MprisPlayerAdaptor::playbackStatus() const {
    if (!m_backend->hasFile()) return QStringLiteral("Stopped");
    return m_backend->isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

QString MprisPlayerAdaptor::loopStatus() const {
    switch (m_backend->repeatMode()) {
    case PlayerBackend::RepeatAll:
        return QStringLiteral("Playlist");
    case PlayerBackend::RepeatOne:
        return QStringLiteral("Track");
    case PlayerBackend::RepeatOff:
    default:
        return QStringLiteral("None");
    }
}

void MprisPlayerAdaptor::setLoopStatus(const QString &status) {
    if (status == QLatin1String("Playlist")) {
        m_backend->setRepeatMode(PlayerBackend::RepeatAll);
    } else if (status == QLatin1String("Track")) {
        m_backend->setRepeatMode(PlayerBackend::RepeatOne);
    } else if (status == QLatin1String("None")) {
        m_backend->setRepeatMode(PlayerBackend::RepeatOff);
    }
}

bool MprisPlayerAdaptor::canGoNext() const {
    return m_backend->canGoNext();
}

bool MprisPlayerAdaptor::canGoPrevious() const {
    return m_backend->canGoPrevious();
}

bool MprisPlayerAdaptor::canPlay() const {
    return m_backend->hasFile() || m_backend->queueCount() > 0;
}

bool MprisPlayerAdaptor::canPause() const {
    return m_backend->hasFile();
}

bool MprisPlayerAdaptor::canSeek() const {
    return m_backend->hasFile() && m_backend->duration() > 0;
}

QString MprisPlayerAdaptor::currentTrackIdPath() const {
    const QString path = m_backend->currentPath();
    if (path != m_lastTrackIdPath) {
        m_lastTrackIdPath = path;
        ++m_trackIdCounter;
    }
    return path.isEmpty()
        ? QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")
        : QStringLiteral("/org/yamp/Track/%1").arg(m_trackIdCounter);
}

QVariantMap MprisPlayerAdaptor::metadata() const {
    if (!m_metadataDirty) return m_cachedMetadata;

    QVariantMap dict;

    const QString trackIdPath = currentTrackIdPath();
    dict.insert(QStringLiteral("mpris:trackid"),
                QVariant::fromValue(QDBusObjectPath(trackIdPath)));

    if (m_backend->currentPath().isEmpty()) {
        m_cachedMetadata = dict;
        m_metadataDirty = false;
        return dict;
    }

    const Track &curTrack = m_backend->currentTrack();
    dict.insert(QStringLiteral("mpris:length"),
                static_cast<qlonglong>(m_backend->duration()) * 1000);
    dict.insert(QStringLiteral("xesam:url"),
                QUrl::fromLocalFile(m_backend->currentPath()).toString());
    dict.insert(QStringLiteral("xesam:title"),  m_backend->currentTitle());

    const QString cur = m_backend->currentArtist();
    if (cur != m_cachedArtist) {
        m_cachedArtist = cur;
        m_cachedArtistList = MusicLibrary::splitArtists(cur);
        if (m_cachedArtistList.isEmpty() && !cur.isEmpty()) m_cachedArtistList.append(cur);
    }
    const QStringList &artists = m_cachedArtistList;
    dict.insert(QStringLiteral("xesam:artist"), artists);

    const QString albumArtist = curTrack.albumArtist.trimmed();
    dict.insert(QStringLiteral("xesam:albumArtist"),
                albumArtist.isEmpty() ? artists : QStringList{albumArtist});
    dict.insert(QStringLiteral("xesam:album"),  m_backend->currentAlbum());

    if (curTrack.trackNo > 0) {
        dict.insert(QStringLiteral("xesam:trackNumber"), curTrack.trackNo);
    }
    if (curTrack.discNo > 0) {
        dict.insert(QStringLiteral("xesam:discNumber"), curTrack.discNo);
    }

    const QString art = m_backend->currentCoverPath();
    if (!art.isEmpty()) {
        dict.insert(QStringLiteral("mpris:artUrl"),
                    QUrl::fromLocalFile(art).toString());
    }

    m_cachedMetadata = dict;
    m_metadataDirty = false;
    return dict;
}

qlonglong MprisPlayerAdaptor::position() const {
    return static_cast<qlonglong>(m_backend->position()) * 1000;
}

double MprisPlayerAdaptor::volume() const { return m_backend->volume(); }

void MprisPlayerAdaptor::setVolume(double v) {
    m_backend->setVolume(qBound(0.0, v, 1.0));
}

bool MprisPlayerAdaptor::shuffle() const { return m_backend->shuffle(); }

void MprisPlayerAdaptor::setShuffle(bool s) {
    m_backend->setShuffle(s);
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    if (!m_backend->hasFile()) return;
    const qint64 durMs = m_backend->duration();
    const qint64 newMs = m_backend->position() + offsetUs / 1000;
    if (durMs > 0 && newMs >= durMs) {
        if (m_backend->canGoNext()) m_backend->playNext();
        else m_backend->stop();
        return;
    }
    m_backend->setPosition(qMax<qint64>(0, newMs));
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs) {
    if (trackId.path() != currentTrackIdPath()) return;
    const qlonglong durationUs = static_cast<qlonglong>(m_backend->duration()) * 1000;
    if (positionUs < 0 || (durationUs > 0 && positionUs > durationUs)) return;
    m_backend->setPosition(positionUs / 1000);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri) {
    if (uri.trimmed().isEmpty()) return;
    m_backend->playMusic(uri);
}
