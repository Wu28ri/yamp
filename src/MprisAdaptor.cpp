#include "MprisAdaptor.h"
#include "MusicLibrary.h"
#include "PlayerBackend.h"

#include <QDateTime>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

namespace {
constexpr const char *kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kPlayerInterface = "org.mpris.MediaPlayer2.Player";
}

MprisRootAdaptor::MprisRootAdaptor(PlayerBackend *backend)
    : QDBusAbstractAdaptor(backend) {}

MprisPlayerAdaptor::MprisPlayerAdaptor(PlayerBackend *backend)
    : QDBusAbstractAdaptor(backend), m_backend(backend) {

    m_metadataTimer = new QTimer(this);
    m_metadataTimer->setSingleShot(true);
    m_metadataTimer->setInterval(50);
    connect(m_metadataTimer, &QTimer::timeout, this, [this]() {
        emitProperties({{QStringLiteral("Metadata"), metadata()}});
    });

    connect(m_backend, &PlayerBackend::metadataChanged, this, &MprisPlayerAdaptor::scheduleMetadataPush);
    connect(m_backend, &PlayerBackend::durationChanged, this, &MprisPlayerAdaptor::scheduleMetadataPush);

    connect(m_backend, &PlayerBackend::playbackStateChanged, this, [this]() {
        emitProperties({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
    });

    connect(m_backend, &PlayerBackend::volumeChanged, this, [this]() {
        emitProperties({{QStringLiteral("Volume"), volume()}});
    });
}

void MprisPlayerAdaptor::scheduleMetadataPush() {
    if (!m_metadataTimer->isActive()) m_metadataTimer->start();
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
    return m_backend->isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

QVariantMap MprisPlayerAdaptor::metadata() const {
    QVariantMap dict;
    dict.insert(QStringLiteral("mpris:trackid"),
                QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Track/0"))));
    dict.insert(QStringLiteral("mpris:length"),
                static_cast<qlonglong>(m_backend->duration()) * 1000);
    dict.insert(QStringLiteral("xesam:title"),  m_backend->currentTitle());
    QStringList artists = MusicLibrary::splitArtists(m_backend->currentArtist());
    if (artists.isEmpty()) artists.append(m_backend->currentArtist());
    dict.insert(QStringLiteral("xesam:artist"), artists);
    dict.insert(QStringLiteral("xesam:albumArtist"), artists);
    dict.insert(QStringLiteral("xesam:album"),  m_backend->currentAlbum());

    const QString art = m_backend->currentCoverPath();
    if (art.isEmpty()) {
        dict.insert(QStringLiteral("mpris:artUrl"), QString());
    } else {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        dict.insert(QStringLiteral("mpris:artUrl"),
                    QStringLiteral("file://%1?t=%2").arg(art).arg(ts));
    }
    return dict;
}

qlonglong MprisPlayerAdaptor::position() const {
    return static_cast<qlonglong>(m_backend->position()) * 1000;
}

double MprisPlayerAdaptor::volume() const { return m_backend->volume(); }

void MprisPlayerAdaptor::setVolume(double v) {
    m_backend->setVolume(qBound(0.0, v, 1.0));
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs) {
    const qint64 newMs = m_backend->position() + offsetUs / 1000;
    m_backend->setPosition(qMax<qint64>(0, newMs));
    emit Seeked(static_cast<qlonglong>(m_backend->position()) * 1000);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs) {
    Q_UNUSED(trackId);
    if (positionUs < 0) return;
    m_backend->setPosition(positionUs / 1000);
    emit Seeked(static_cast<qlonglong>(m_backend->position()) * 1000);
}
