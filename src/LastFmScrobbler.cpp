#include "LastFmScrobbler.h"

#include "PlayerBackend.h"
#include "Settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <utility>

namespace {
#ifndef YAMP_VERSION
#define YAMP_VERSION "0.2"
#endif

constexpr const char *kApiKey       = "1af929173f9ea4c2116ed32d25f8db59";
constexpr const char *kSharedSecret = "515f52b0aa9f97182aa4641ce1660d15";
constexpr const char *kApiRoot      = "https://ws.audioscrobbler.com/2.0/";
constexpr const char *kUserAgent    = "YAMP/" YAMP_VERSION " (+https://github.com/Wu28ri/yamp)";

constexpr int kRequestTimeoutMs    = 15000;
constexpr int kAuthPollMs          = 2500;
constexpr int kAuthDeadlineMs      = 5 * 60 * 1000;
constexpr int kNowPlayingRetryMs   = 15000;
constexpr int kMaxPendingScrobbles = 500;
constexpr int kMaxScrobbleBatch    = 50;
constexpr int kMaxScrobbleRetries  = 12;

QByteArray utf8(const QString &s) { return s.toUtf8(); }

bool isScrobblableTrack(const Track &track) {
    if (!track.isValid()) return false;
    const QString artist = track.artist.trimmed();
    const QString title  = track.title.trimmed();
    if (artist.isEmpty() || title.isEmpty()) return false;
    if (artist.compare(QStringLiteral("Unknown Artist"), Qt::CaseInsensitive) == 0 ||
        artist.compare(QStringLiteral("[no artist]"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    if (title == QLatin1String("N/A")) return false;
    return true;
}

QMap<QString, QString> trackParameters(const Track &track, const QString &suffix = {}) {
    QMap<QString, QString> params{{QStringLiteral("artist") + suffix, track.artist},
                                  {QStringLiteral("track") + suffix, track.title}};
    if (!track.album.isEmpty() &&
        track.album.compare(QStringLiteral("Unknown Album"), Qt::CaseInsensitive) != 0)
        params.insert(QStringLiteral("album") + suffix, track.album);
    if (!track.albumArtist.isEmpty() && track.albumArtist != track.artist &&
        track.albumArtist.compare(QStringLiteral("Unknown Artist"), Qt::CaseInsensitive) != 0)
        params.insert(QStringLiteral("albumArtist") + suffix, track.albumArtist);
    if (track.duration > 0)
        params.insert(QStringLiteral("duration") + suffix, QString::number(track.duration));
    if (track.trackNo > 0)
        params.insert(QStringLiteral("trackNumber") + suffix, QString::number(track.trackNo));
    return params;
}

int jsonInt(const QJsonValue &value, int fallback = -1) {
    if (value.isDouble()) return value.toInt(fallback);
    if (value.isString()) {
        bool ok = false;
        const int result = value.toString().toInt(&ok);
        if (ok) return result;
    }
    return fallback;
}

int apiErrorCode(const QJsonObject &object) {
    return jsonInt(object.value(QStringLiteral("error")), 0);
}

QString apiErrorMessage(const QJsonObject &object, const QString &fallback) {
    return object.value(QStringLiteral("message")).toString(fallback);
}

bool transientApiError(int code) {
    return code == 8 || code == 11 || code == 16 || code == 29;
}

struct ParsedResponse {
    QJsonObject object;
    bool valid = false;
};

ParsedResponse parseResponse(const QByteArray &payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    return {document.object(), error.error == QJsonParseError::NoError && document.isObject()};
}

void armHardTimeout(QNetworkReply *reply) {
    if (!reply) return;
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setInterval(kRequestTimeoutMs);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    timer->start();
}
}

LastFmScrobbler::LastFmScrobbler(PlayerBackend *backend, Settings *settings, QObject *parent)
    : QObject(parent), m_backend(backend), m_settings(settings),
      m_nam(new QNetworkAccessManager(this)),
      m_authPollTimer(new QTimer(this)),
      m_nowPlayingRetryTimer(new QTimer(this)),
      m_scrobbleRetryTimer(new QTimer(this))
{
    m_sessionKey = settings->lastfmSessionKey();
    m_username   = settings->lastfmUsername();
    m_enabled    = settings->lastfmEnabled();
    restoreScrobbleQueue();

    m_authPollTimer->setSingleShot(true);
    connect(m_authPollTimer, &QTimer::timeout, this, &LastFmScrobbler::pollAuthSession);

    m_nowPlayingRetryTimer->setSingleShot(true);
    connect(m_nowPlayingRetryTimer, &QTimer::timeout,
            this, &LastFmScrobbler::maybeSendNowPlaying);

    m_scrobbleRetryTimer->setSingleShot(true);
    connect(m_scrobbleRetryTimer, &QTimer::timeout,
            this, &LastFmScrobbler::processScrobbleQueue);

    connect(backend, &PlayerBackend::trackStarted,
            this, &LastFmScrobbler::onTrackStarted);
    connect(backend, &PlayerBackend::playbackStateChanged,
            this, &LastFmScrobbler::onPlaybackStateChanged);
    connect(backend, &PlayerBackend::positionChanged,
            this, &LastFmScrobbler::onPositionChanged);

    if (m_enabled && authenticated()) {
        QTimer::singleShot(0, this, &LastFmScrobbler::processScrobbleQueue);
    }
}

void LastFmScrobbler::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    if (!enabled) {
        accumulatePlayTime();
        maybeScrobble();
        m_enabled = false;
        m_isPlaying = false;
        m_accumulatedMs = 0;
        m_trackedStartedAtUnix = 0;
        m_playTimer.invalidate();
        m_nowPlayingRetryTimer->stop();
    } else {
        m_enabled = true;
        if (m_backend && m_backend->hasFile() && m_backend->currentTrack().isValid()) {
            if (m_trackedTrack.path == m_backend->currentTrack().path) {
                if (!m_scrobbleQueued) {
                    m_trackedOwner = m_username;
                    m_trackedStartedAtUnix = QDateTime::currentSecsSinceEpoch();
                    m_accumulatedMs = 0;
                }
                m_isPlaying = m_backend->isPlaying();
                if (m_isPlaying) m_playTimer.start();
            } else {
                startTracking(m_backend->currentTrack());
            }
        }
        if (authenticated()) {
            maybeSendNowPlaying();
            processScrobbleQueue();
        }
    }
    m_settings->setLastfmEnabled(enabled);
    emit enabledChanged();
}

void LastFmScrobbler::setPendingToken(const QString &token) {
    m_pendingToken = token;
}

void LastFmScrobbler::setAuthActive(bool active) {
    if (m_authActive == active) return;
    m_authActive = active;
    emit awaitingAuthChanged();
}

void LastFmScrobbler::finishAuth() {
    m_authPollTimer->stop();
    m_authReply = nullptr;
    setPendingToken(QString());
    setAuthActive(false);
}

QString LastFmScrobbler::signature(const QMap<QString, QString> &params) const {
    QByteArray buffer;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == QLatin1String("format") || it.key() == QLatin1String("callback"))
            continue;
        buffer += utf8(it.key());
        buffer += utf8(it.value());
    }
    buffer += utf8(QString::fromLatin1(kSharedSecret));
    return QString::fromLatin1(
        QCryptographicHash::hash(buffer, QCryptographicHash::Md5).toHex());
}

QNetworkReply *LastFmScrobbler::requestSigned(QMap<QString, QString> params, bool post) {
    params.insert(QStringLiteral("api_key"), QString::fromLatin1(kApiKey));
    params.insert(QStringLiteral("api_sig"), signature(params));
    params.insert(QStringLiteral("format"), QStringLiteral("json"));

    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value());
    const QString encoded = query.toString(QUrl::FullyEncoded)
        .replace(QLatin1Char('+'), QStringLiteral("%2B"));

    QUrl url(QString::fromLatin1(kApiRoot));
    if (!post) url.setQuery(encoded);
    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeoutMs);
    request.setRawHeader("User-Agent", kUserAgent);
    QNetworkReply *reply;
    if (post) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        reply = m_nam->post(request, encoded.toUtf8());
    } else {
        reply = m_nam->get(request);
    }
    armHardTimeout(reply);
    return reply;
}

void LastFmScrobbler::startAuth() {
    if (m_authActive) return;

    const quint64 generation = ++m_authGeneration;
    m_authElapsed.start();
    setAuthActive(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("auth.getToken"));
    QNetworkReply *reply = requestSigned(params, false);
    m_authReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const ParsedResponse response = parseResponse(reply->readAll());
        const QJsonObject &object = response.object;
        reply->deleteLater();

        if (generation != m_authGeneration || !m_authActive) return;
        if (m_authReply == reply) m_authReply = nullptr;

        const int apiError = apiErrorCode(object);
        if (apiError != 0) {
            finishAuth();
            return;
        }
        if (networkError != QNetworkReply::NoError || !response.valid) {
            finishAuth();
            return;
        }

        const QString token = object.value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            finishAuth();
            return;
        }

        setPendingToken(token);
        QUrl authUrl(QStringLiteral("https://www.last.fm/api/auth/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("api_key"), QString::fromLatin1(kApiKey));
        query.addQueryItem(QStringLiteral("token"), token);
        authUrl.setQuery(query);
        QDesktopServices::openUrl(authUrl);
        scheduleAuthPoll();
    });
}

void LastFmScrobbler::cancelAuth() {
    if (!m_authActive) return;

    ++m_authGeneration;
    m_authPollTimer->stop();
    if (m_authReply) {
        QNetworkReply *reply = m_authReply;
        m_authReply = nullptr;
        reply->abort();
    }
    setPendingToken(QString());
    setAuthActive(false);
}

void LastFmScrobbler::scheduleAuthPoll() {
    if (!m_authActive || m_pendingToken.isEmpty()) return;
    if (m_authElapsed.elapsed() >= kAuthDeadlineMs) {
        finishAuth();
        return;
    }
    m_authPollTimer->start(kAuthPollMs);
}

void LastFmScrobbler::pollAuthSession() {
    if (!m_authActive || m_pendingToken.isEmpty() || m_authReply) return;
    if (m_authElapsed.elapsed() >= kAuthDeadlineMs) {
        finishAuth();
        return;
    }

    const quint64 generation = m_authGeneration;
    const QString token = m_pendingToken;
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("auth.getSession"));
    params.insert(QStringLiteral("token"), token);
    QNetworkReply *reply = requestSigned(params, false);
    m_authReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, token]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkMessage = reply->errorString();
        const ParsedResponse response = parseResponse(reply->readAll());
        const QJsonObject &object = response.object;
        reply->deleteLater();

        if (generation != m_authGeneration || !m_authActive || token != m_pendingToken)
            return;
        if (m_authReply == reply) m_authReply = nullptr;

        const int apiError = apiErrorCode(object);
        if (apiError == 14) {
            scheduleAuthPoll();
            return;
        }
        if (transientApiError(apiError)) {
            scheduleAuthPoll();
            return;
        }
        if (apiError != 0) {
            finishAuth();
            return;
        }
        if (networkError != QNetworkReply::NoError || !response.valid) {
            qWarning("Last.fm authorization poll failed: %s", qUtf8Printable(networkMessage));
            scheduleAuthPoll();
            return;
        }

        const QJsonObject session = object.value(QStringLiteral("session")).toObject();
        const QString key  = session.value(QStringLiteral("key")).toString();
        const QString name = session.value(QStringLiteral("name")).toString();
        if (key.isEmpty()) {
            scheduleAuthPoll();
            return;
        }

        m_authPollTimer->stop();
        for (qsizetype i = m_pendingScrobbles.size(); i > 0; --i) {
            const qsizetype index = i - 1;
            const QString &owner = m_pendingScrobbles.at(index).owner;
            if (!owner.isEmpty() && owner != name) m_pendingScrobbles.removeAt(index);
        }
        saveScrobbleQueue();
        m_sessionKey = key;
        m_username   = name;
        m_settings->setLastfmSessionKey(key);
        m_settings->setLastfmUsername(name);
        setPendingToken(QString());
        setAuthActive(false);
        if (m_trackedOwner != name && m_backend && m_backend->hasFile())
            startTracking(m_backend->currentTrack());
        emit authStateChanged();
        maybeSendNowPlaying();
        processScrobbleQueue();
    });
}

void LastFmScrobbler::logout() {
    ++m_authGeneration;
    m_authPollTimer->stop();
    if (m_authReply) {
        QNetworkReply *reply = m_authReply;
        m_authReply = nullptr;
        reply->abort();
    }
    setPendingToken(QString());
    setAuthActive(false);
    ++m_playbackGeneration;
    m_nowPlayingRetryTimer->stop();
    m_scrobbleRetryTimer->stop();
    m_nowPlayingInFlight = false;
    m_scrobbleInFlightIds.clear();
    m_pendingScrobbles.clear();
    saveScrobbleQueue();
    resetTrackedTrack();

    m_sessionKey.clear();
    m_username.clear();
    m_settings->setLastfmSessionKey(QString());
    m_settings->setLastfmUsername(QString());
    m_nowPlayingSent = false;
    emit authStateChanged();
}

void LastFmScrobbler::invalidateSession(const QString &expectedKey) {
    if (expectedKey.isEmpty() || expectedKey != m_sessionKey) return;

    ++m_playbackGeneration;
    m_sessionKey.clear();
    m_username.clear();
    m_settings->setLastfmSessionKey(QString());
    m_settings->setLastfmUsername(QString());
    m_nowPlayingSent = false;
    m_nowPlayingInFlight = false;
    m_nowPlayingRetryTimer->stop();
    emit authStateChanged();
}

void LastFmScrobbler::resetTrackedTrack() {
    ++m_playbackGeneration;
    m_nowPlayingRetryTimer->stop();
    m_trackedTrack = Track();
    m_trackedOwner.clear();
    m_trackedStartedAtUnix = 0;
    m_accumulatedMs        = 0;
    m_playTimer.invalidate();
    m_isPlaying            = false;
    m_nowPlayingSent       = false;
    m_nowPlayingInFlight   = false;
    m_scrobbleQueued       = false;
}

void LastFmScrobbler::startTracking(const Track &track) {
    resetTrackedTrack();
    if (!m_enabled || !track.isValid()) return;

    m_trackedTrack = track;
    m_trackedOwner = m_username;
    m_trackedStartedAtUnix = QDateTime::currentSecsSinceEpoch();
    m_isPlaying = m_backend && m_backend->isPlaying();
    if (m_isPlaying) m_playTimer.start();
    maybeSendNowPlaying();
}

void LastFmScrobbler::accumulatePlayTime() {
    if (!m_isPlaying) return;
    if (!m_playTimer.isValid()) {
        m_playTimer.start();
        return;
    }
    m_accumulatedMs += qMax<qint64>(0, m_playTimer.restart());
}

void LastFmScrobbler::maybeScrobble() {
    if (m_scrobbleQueued || !m_enabled) return;
    if (!isScrobblableTrack(m_trackedTrack)) return;
    if (m_trackedOwner.isEmpty()) return;
    if (m_trackedTrack.duration < 30) return;

    const qint64 halfMs = qint64(m_trackedTrack.duration) * 500;
    const qint64 threshold = qMin<qint64>(halfMs, 240 * 1000);
    if (m_accumulatedMs < threshold) return;

    m_scrobbleQueued = true;
    PendingScrobble pending;
    pending.id = ++m_nextScrobbleId;
    pending.track = m_trackedTrack;
    pending.owner = m_trackedOwner;
    pending.startedAtUnix = m_trackedStartedAtUnix;
    m_pendingScrobbles.append(pending);
    while (m_pendingScrobbles.size() > kMaxPendingScrobbles) {
        const quint64 oldestId = m_pendingScrobbles.constFirst().id;
        if (m_scrobbleInFlightIds.contains(oldestId)) break;
        m_pendingScrobbles.removeFirst();
    }
    saveScrobbleQueue();
    processScrobbleQueue();
}

void LastFmScrobbler::onTrackStarted(const Track &track) {
    accumulatePlayTime();
    maybeScrobble();
    startTracking(track);
}

void LastFmScrobbler::onPlaybackStateChanged() {
    if (!m_backend) return;
    if (!m_enabled) {
        m_isPlaying = false;
        m_playTimer.invalidate();
        return;
    }
    const bool nowPlaying = m_backend->isPlaying();
    if (nowPlaying == m_isPlaying) return;

    accumulatePlayTime();
    maybeScrobble();
    m_isPlaying = nowPlaying;
    if (nowPlaying) {
        m_playTimer.start();
        maybeSendNowPlaying();
    } else {
        m_playTimer.invalidate();
    }
}

void LastFmScrobbler::onPositionChanged() {
    if (!m_enabled) return;
    accumulatePlayTime();
    maybeScrobble();
}

void LastFmScrobbler::maybeSendNowPlaying() {
    if (!m_enabled || !authenticated() || !m_isPlaying ||
        m_nowPlayingSent || m_nowPlayingInFlight)
        return;
    if (m_nowPlayingRetryTimer->isActive()) return;
    if (!isScrobblableTrack(m_trackedTrack)) return;

    const Track track = m_trackedTrack;
    const quint64 generation = m_playbackGeneration;
    const QString sessionKey = m_sessionKey;

    QMap<QString, QString> params = trackParameters(track);
    params.insert(QStringLiteral("method"), QStringLiteral("track.updateNowPlaying"));
    params.insert(QStringLiteral("sk"), sessionKey);

    m_nowPlayingInFlight = true;
    QNetworkReply *reply = requestSigned(params);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, sessionKey]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkMessage = reply->errorString();
        const ParsedResponse response = parseResponse(reply->readAll());
        const QJsonObject &object = response.object;
        reply->deleteLater();

        if (generation != m_playbackGeneration) return;
        m_nowPlayingInFlight = false;
        if (sessionKey != m_sessionKey) {
            maybeSendNowPlaying();
            return;
        }

        const int apiError = apiErrorCode(object);
        if (apiError == 9) {
            invalidateSession(sessionKey);
            return;
        }
        if (transientApiError(apiError)) {
            const QString message = apiError != 0
                ? apiErrorMessage(object, QStringLiteral("unknown error"))
                : networkMessage;
            qWarning("Last.fm now playing failed: %s", qUtf8Printable(message));
            m_nowPlayingRetryTimer->start(kNowPlayingRetryMs);
            return;
        }
        if (apiError != 0) {
            m_nowPlayingSent = true;
            return;
        }
        if (!response.valid || !object.value(QStringLiteral("nowplaying")).isObject()) {
            const QString message = networkError == QNetworkReply::NoError
                ? QStringLiteral("invalid response") : networkMessage;
            qWarning("Last.fm now playing failed: %s", qUtf8Printable(message));
            m_nowPlayingRetryTimer->start(kNowPlayingRetryMs);
            return;
        }
        m_nowPlayingSent = true;
    });
}

void LastFmScrobbler::processScrobbleQueue() {
    if (!m_enabled || !authenticated() || m_pendingScrobbles.isEmpty()) return;
    if (!m_scrobbleInFlightIds.isEmpty() || m_scrobbleRetryTimer->isActive()) return;

    bool pruned = false;
    while (!m_pendingScrobbles.isEmpty()) {
        const PendingScrobble &head = m_pendingScrobbles.constFirst();
        if ((!head.owner.isEmpty() && head.owner != m_username) ||
            !isScrobblableTrack(head.track)) {
            m_pendingScrobbles.removeFirst();
            pruned = true;
            continue;
        }
        break;
    }
    if (pruned) saveScrobbleQueue();
    if (m_pendingScrobbles.isEmpty()) return;

    const QString sessionKey = m_sessionKey;
    QMap<QString, QString> params;
    params.insert(QStringLiteral("method"), QStringLiteral("track.scrobble"));
    params.insert(QStringLiteral("sk"), sessionKey);

    QList<quint64> batchIds;
    const int batchCount = qMin<int>(m_pendingScrobbles.size(), kMaxScrobbleBatch);
    int idx = 0;
    for (int i = 0; i < batchCount; ++i) {
        const PendingScrobble &item = m_pendingScrobbles.at(i);
        if (!item.owner.isEmpty() && item.owner != m_username) break;
        if (!isScrobblableTrack(item.track)) break;

        const QString suffix = QStringLiteral("[%1]").arg(idx);
        const auto itemParams = trackParameters(item.track, suffix);
        for (auto it = itemParams.constBegin(); it != itemParams.constEnd(); ++it) {
            params.insert(it.key(), it.value());
        }
        params.insert(QStringLiteral("timestamp") + suffix, QString::number(item.startedAtUnix));
        batchIds.append(item.id);
        ++idx;
    }

    if (batchIds.isEmpty()) return;
    m_scrobbleInFlightIds = batchIds;

    QNetworkReply *reply = requestSigned(params);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, batchIds, sessionKey]() {
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkMessage = reply->errorString();
        const ParsedResponse response = parseResponse(reply->readAll());
        const QJsonObject &object = response.object;
        reply->deleteLater();

        if (m_scrobbleInFlightIds != batchIds) return;
        m_scrobbleInFlightIds.clear();
        if (m_pendingScrobbles.isEmpty()) return;
        if (sessionKey != m_sessionKey) {
            QTimer::singleShot(0, this, &LastFmScrobbler::processScrobbleQueue);
            return;
        }

        auto removeBatchIds = [this, &batchIds]() {
            for (qsizetype i = m_pendingScrobbles.size(); i > 0; --i) {
                if (batchIds.contains(m_pendingScrobbles.at(i - 1).id)) {
                    m_pendingScrobbles.removeAt(i - 1);
                }
            }
            saveScrobbleQueue();
        };

        const int apiError = apiErrorCode(object);
        if (apiError == 9) {
            invalidateSession(sessionKey);
            return;
        }
        if (transientApiError(apiError)) {
            const QString message = apiError != 0
                ? apiErrorMessage(object, QStringLiteral("temporary Last.fm error"))
                : networkMessage;
            qWarning("Last.fm scrobble failed, will retry: %s", qUtf8Printable(message));
            scheduleScrobbleRetry(true);
            return;
        }
        if (apiError != 0) {
            removeBatchIds();
            QTimer::singleShot(0, this, &LastFmScrobbler::processScrobbleQueue);
            return;
        }

        const QJsonObject scrobbles = object.value(QStringLiteral("scrobbles")).toObject();
        const QJsonObject attributes = scrobbles.value(QStringLiteral("@attr")).toObject();
        if (!response.valid || attributes.isEmpty()) {
            const bool networkFailure = (networkError != QNetworkReply::NoError);
            const QString message = !networkFailure
                ? QStringLiteral("invalid response") : networkMessage;
            qWarning("Last.fm scrobble failed, will retry: %s", qUtf8Printable(message));
            scheduleScrobbleRetry(networkFailure);
            return;
        }
        removeBatchIds();
        QTimer::singleShot(0, this, &LastFmScrobbler::processScrobbleQueue);
    });
}

void LastFmScrobbler::scheduleScrobbleRetry(bool transient) {
    if (m_pendingScrobbles.isEmpty()) return;
    PendingScrobble &pending = m_pendingScrobbles.first();
    ++pending.retryCount;
    if (!transient && pending.retryCount > kMaxScrobbleRetries) {
        m_pendingScrobbles.removeFirst();
        saveScrobbleQueue();
        QTimer::singleShot(0, this, &LastFmScrobbler::processScrobbleQueue);
        return;
    }
    const int shift = qMin(pending.retryCount - 1, 6);
    const int delayMs = qMin(5 * 60 * 1000, 5000 * (1 << shift));
    if (pending.retryCount <= kMaxScrobbleRetries) saveScrobbleQueue();
    m_scrobbleRetryTimer->start(delayMs);
}

void LastFmScrobbler::restoreScrobbleQueue() {
    const QJsonDocument document = QJsonDocument::fromJson(m_settings->lastfmPendingQueue());
    if (!document.isArray()) return;
    for (const QJsonValue value : document.array()) {
        const QJsonObject object = value.toObject();
        PendingScrobble pending;
        pending.id = object.value(QStringLiteral("id")).toString().toULongLong();
        pending.owner = object.value(QStringLiteral("owner")).toString();
        pending.startedAtUnix = object.value(QStringLiteral("startedAt")).toString().toLongLong();
        pending.retryCount = object.value(QStringLiteral("retryCount")).toInt();
        const QJsonObject track = object.value(QStringLiteral("track")).toObject();
        pending.track.path = track.value(QStringLiteral("path")).toString();
        pending.track.title = track.value(QStringLiteral("title")).toString();
        pending.track.artist = track.value(QStringLiteral("artist")).toString();
        pending.track.albumArtist = track.value(QStringLiteral("albumArtist")).toString();
        pending.track.album = track.value(QStringLiteral("album")).toString();
        pending.track.duration = track.value(QStringLiteral("duration")).toInt();
        pending.track.trackNo = track.value(QStringLiteral("trackNo")).toInt();
        if (pending.id == 0 || pending.owner.isEmpty() ||
            !isScrobblableTrack(pending.track)) continue;
        if (m_pendingScrobbles.size() >= kMaxPendingScrobbles) {
            m_pendingScrobbles.removeFirst();
        }
        m_pendingScrobbles.append(pending);
        m_nextScrobbleId = qMax(m_nextScrobbleId, pending.id);
    }
}

void LastFmScrobbler::saveScrobbleQueue() {
    QJsonArray array;
    for (const PendingScrobble &pending : std::as_const(m_pendingScrobbles)) {
        QJsonObject track;
        track.insert(QStringLiteral("path"), pending.track.path);
        track.insert(QStringLiteral("title"), pending.track.title);
        track.insert(QStringLiteral("artist"), pending.track.artist);
        track.insert(QStringLiteral("albumArtist"), pending.track.albumArtist);
        track.insert(QStringLiteral("album"), pending.track.album);
        track.insert(QStringLiteral("duration"), pending.track.duration);
        track.insert(QStringLiteral("trackNo"), pending.track.trackNo);

        QJsonObject object;
        object.insert(QStringLiteral("id"), QString::number(pending.id));
        object.insert(QStringLiteral("owner"), pending.owner);
        object.insert(QStringLiteral("startedAt"), QString::number(pending.startedAtUnix));
        object.insert(QStringLiteral("retryCount"), pending.retryCount);
        object.insert(QStringLiteral("track"), track);
        array.append(object);
    }
    m_settings->setLastfmPendingQueue(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
