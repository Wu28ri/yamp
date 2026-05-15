#include "LastFmScrobbler.h"

#include "PlayerBackend.h"
#include "settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

namespace {
constexpr const char *kApiKey       = "1af929173f9ea4c2116ed32d25f8db59";
constexpr const char *kSharedSecret = "515f52b0aa9f97182aa4641ce1660d15";
constexpr const char *kApiRoot      = "https://ws.audioscrobbler.com/2.0/";

QByteArray utf8(const QString &s) { return s.toUtf8(); }
}

LastFmScrobbler::LastFmScrobbler(PlayerBackend *backend, Settings *settings, QObject *parent)
    : QObject(parent), m_backend(backend), m_settings(settings),
      m_nam(new QNetworkAccessManager(this)),
      m_authPollTimer(new QTimer(this))
{
    m_sessionKey = settings->lastfmSessionKey();
    m_username   = settings->lastfmUsername();
    m_enabled    = settings->lastfmEnabled();

    m_authPollTimer->setInterval(2500);
    connect(m_authPollTimer, &QTimer::timeout, this, &LastFmScrobbler::pollAuthSession);

    connect(backend, &PlayerBackend::metadataChanged,      this, &LastFmScrobbler::onMetadataChanged);
    connect(backend, &PlayerBackend::playbackStateChanged, this, &LastFmScrobbler::onPlaybackStateChanged);
    connect(backend, &PlayerBackend::positionChanged,      this, &LastFmScrobbler::onPositionChanged);

    if (m_enabled && authenticated()) {
        setStatus(QStringLiteral("Connected as %1").arg(m_username));
    } else if (m_enabled) {
        setStatus(QStringLiteral("Not connected"));
    }
}

void LastFmScrobbler::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    m_settings->setLastfmEnabled(enabled);
    if (!enabled) {
        resetTrackedTrack();
        setStatus(QStringLiteral("Scrobbling disabled"));
    } else if (authenticated()) {
        setStatus(QStringLiteral("Connected as %1").arg(m_username));
    } else {
        setStatus(QStringLiteral("Not connected"));
    }
    emit enabledChanged();
}

void LastFmScrobbler::setStatus(const QString &s) {
    if (m_status == s) return;
    m_status = s;
    emit statusChanged();
}

void LastFmScrobbler::setPendingToken(const QString &t) {
    const bool was = !m_pendingToken.isEmpty();
    m_pendingToken = t;
    const bool now = !m_pendingToken.isEmpty();
    if (was != now) emit awaitingAuthChanged();
}

QString LastFmScrobbler::signature(const QMap<QString, QString> &params) const {
    QByteArray buf;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == QLatin1String("format") || it.key() == QLatin1String("callback"))
            continue;
        buf += utf8(it.key());
        buf += utf8(it.value());
    }
    buf += utf8(QString::fromLatin1(kSharedSecret));
    return QString::fromLatin1(
        QCryptographicHash::hash(buf, QCryptographicHash::Md5).toHex());
}

QNetworkReply* LastFmScrobbler::postSigned(QMap<QString, QString> params) {
    params.insert(QStringLiteral("api_key"), QString::fromLatin1(kApiKey));
    const QString sig = signature(params);
    params.insert(QStringLiteral("api_sig"), sig);
    params.insert(QStringLiteral("format"), QStringLiteral("json"));

    QUrlQuery body;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        body.addQueryItem(it.key(), it.value());

    QNetworkRequest req((QUrl(QString::fromLatin1(kApiRoot))));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    return m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
}

QNetworkReply* LastFmScrobbler::getSigned(QMap<QString, QString> params) {
    params.insert(QStringLiteral("api_key"), QString::fromLatin1(kApiKey));
    const QString sig = signature(params);
    params.insert(QStringLiteral("api_sig"), sig);
    params.insert(QStringLiteral("format"), QStringLiteral("json"));

    QUrl url(QString::fromLatin1(kApiRoot));
    QUrlQuery q;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        q.addQueryItem(it.key(), it.value());
    url.setQuery(q);
    return m_nam->get(QNetworkRequest(url));
}

void LastFmScrobbler::startAuth() {
    setStatus(QStringLiteral("Requesting token..."));
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("auth.getToken"));
    QNetworkReply *reply = getSigned(params);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QStringLiteral("Token request failed: %1").arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString token = doc.object().value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            setStatus(QStringLiteral("No token in response"));
            return;
        }
        setPendingToken(token);
        QUrl authUrl(QStringLiteral("https://www.last.fm/api/auth/"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), QString::fromLatin1(kApiKey));
        q.addQueryItem(QStringLiteral("token"), token);
        authUrl.setQuery(q);
        QDesktopServices::openUrl(authUrl);
        setStatus(QStringLiteral("Waiting for browser authorization..."));
        m_authPollsLeft = 120; // ~5 minutes at 2.5s
        m_authPollTimer->start();
    });
}

void LastFmScrobbler::cancelAuth() {
    m_authPollTimer->stop();
    setPendingToken(QString());
    setStatus(QStringLiteral("Authorization cancelled"));
}

void LastFmScrobbler::pollAuthSession() {
    if (m_pendingToken.isEmpty()) {
        m_authPollTimer->stop();
        return;
    }
    if (--m_authPollsLeft <= 0) {
        m_authPollTimer->stop();
        setPendingToken(QString());
        setStatus(QStringLiteral("Authorization timed out"));
        return;
    }

    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("auth.getSession"));
    params.insert(QStringLiteral("token"), m_pendingToken);
    QNetworkReply *reply = getSigned(params);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const int err = obj.value(QStringLiteral("error")).toInt();
            if (err == 14) return; // not yet authorized — keep polling
            m_authPollTimer->stop();
            setPendingToken(QString());
            const QString msg = obj.value(QStringLiteral("message")).toString(
                QStringLiteral("unknown error"));
            setStatus(QStringLiteral("Auth failed: %1").arg(msg));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) return; // network blip, retry

        const QJsonObject session = obj.value(QStringLiteral("session")).toObject();
        const QString key  = session.value(QStringLiteral("key")).toString();
        const QString name = session.value(QStringLiteral("name")).toString();
        if (key.isEmpty()) return; // unexpected, keep trying

        m_authPollTimer->stop();
        m_sessionKey = key;
        m_username   = name;
        m_settings->setLastfmSessionKey(key);
        m_settings->setLastfmUsername(name);
        setPendingToken(QString());
        setStatus(QStringLiteral("Connected as %1").arg(name));
        emit authStateChanged();
    });
}

void LastFmScrobbler::logout() {
    m_authPollTimer->stop();
    m_sessionKey.clear();
    m_username.clear();
    m_settings->setLastfmSessionKey(QString());
    m_settings->setLastfmUsername(QString());
    setPendingToken(QString());
    setStatus(QStringLiteral("Disconnected"));
    emit authStateChanged();
}

void LastFmScrobbler::resetTrackedTrack() {
    m_trackedTrack = Track();
    m_trackedStartedAtUnix = 0;
    m_accumulatedMs        = 0;
    m_lastPlayingWallMs    = 0;
    m_nowPlayingSent       = false;
    m_scrobbled            = false;
}

void LastFmScrobbler::accumulatePlayTime() {
    if (!m_isPlaying) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPlayingWallMs > 0)
        m_accumulatedMs += now - m_lastPlayingWallMs;
    m_lastPlayingWallMs = now;
}

void LastFmScrobbler::maybeScrobble() {
    if (m_scrobbled) return;
    if (!m_trackedTrack.isValid()) return;
    if (m_trackedTrack.artist.isEmpty() || m_trackedTrack.title.isEmpty()) return;
    if (m_trackedTrack.duration < 30) return;

    const qint64 halfMs   = qint64(m_trackedTrack.duration) * 500;
    const qint64 fourMins = 240 * 1000;
    const qint64 threshold = qMin(halfMs, fourMins);
    if (m_accumulatedMs < threshold) return;

    m_scrobbled = true;
    if (m_enabled && authenticated())
        sendScrobble(m_trackedTrack, m_trackedStartedAtUnix);
}

void LastFmScrobbler::onMetadataChanged() {
    if (!m_backend) return;
    const Track &cur = m_backend->currentTrack();

    if (cur.path == m_trackedTrack.path) return;

    accumulatePlayTime();
    maybeScrobble();
    resetTrackedTrack();

    if (!cur.isValid()) return;

    m_trackedTrack         = cur;
    m_trackedStartedAtUnix = QDateTime::currentSecsSinceEpoch();
    m_isPlaying            = m_backend->isPlaying();
    m_lastPlayingWallMs    = m_isPlaying ? QDateTime::currentMSecsSinceEpoch() : 0;

    if (m_enabled && authenticated() && !m_nowPlayingSent) {
        sendNowPlaying(m_trackedTrack);
        m_nowPlayingSent = true;
    }
}

void LastFmScrobbler::onPlaybackStateChanged() {
    if (!m_backend) return;
    const bool nowPlaying = m_backend->isPlaying();
    if (nowPlaying == m_isPlaying) return;

    accumulatePlayTime();
    m_isPlaying = nowPlaying;
    m_lastPlayingWallMs = nowPlaying ? QDateTime::currentMSecsSinceEpoch() : 0;
}

void LastFmScrobbler::onPositionChanged() {
    accumulatePlayTime();
    maybeScrobble();
}

void LastFmScrobbler::sendNowPlaying(const Track &t) {
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("track.updateNowPlaying"));
    params.insert(QStringLiteral("sk"),     m_sessionKey);
    params.insert(QStringLiteral("artist"), t.artist);
    params.insert(QStringLiteral("track"),  t.title);
    if (!t.album.isEmpty())
        params.insert(QStringLiteral("album"), t.album);
    if (!t.albumArtist.isEmpty() && t.albumArtist != t.artist)
        params.insert(QStringLiteral("albumArtist"), t.albumArtist);
    if (t.duration > 0)
        params.insert(QStringLiteral("duration"), QString::number(t.duration));
    if (t.trackNo > 0)
        params.insert(QStringLiteral("trackNumber"), QString::number(t.trackNo));

    QNetworkReply *reply = postSigned(params);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("LastFm now playing failed: %s",
                     qUtf8Printable(reply->errorString()));
        }
    });
}

void LastFmScrobbler::sendScrobble(const Track &t, qint64 startedAtUnix) {
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"),       QStringLiteral("track.scrobble"));
    params.insert(QStringLiteral("sk"),           m_sessionKey);
    params.insert(QStringLiteral("artist[0]"),    t.artist);
    params.insert(QStringLiteral("track[0]"),     t.title);
    params.insert(QStringLiteral("timestamp[0]"), QString::number(startedAtUnix));
    if (!t.album.isEmpty())
        params.insert(QStringLiteral("album[0]"), t.album);
    if (!t.albumArtist.isEmpty() && t.albumArtist != t.artist)
        params.insert(QStringLiteral("albumArtist[0]"), t.albumArtist);
    if (t.duration > 0)
        params.insert(QStringLiteral("duration[0]"), QString::number(t.duration));
    if (t.trackNo > 0)
        params.insert(QStringLiteral("trackNumber[0]"), QString::number(t.trackNo));

    QNetworkReply *reply = postSigned(params);
    connect(reply, &QNetworkReply::finished, this, [this, reply, t]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("LastFm scrobble failed: %s",
                     qUtf8Printable(reply->errorString()));
            setStatus(QStringLiteral("Scrobble failed: %1").arg(reply->errorString()));
            return;
        }
        setStatus(QStringLiteral("Scrobbled: %1 — %2").arg(t.artist, t.title));
    });
}
