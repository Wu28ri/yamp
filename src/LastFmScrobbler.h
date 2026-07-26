#pragma once

#include "Track.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QMap>

class PlayerBackend;
class Settings;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class LastFmScrobbler : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    enabled        READ enabled       WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool    authenticated  READ authenticated NOTIFY authStateChanged)
    Q_PROPERTY(QString username       READ username      NOTIFY authStateChanged)
    Q_PROPERTY(bool    awaitingAuth   READ awaitingAuth  NOTIFY awaitingAuthChanged)

public:
    explicit LastFmScrobbler(PlayerBackend *backend, Settings *settings,
                             QObject *parent = nullptr);

    bool    enabled()       const { return m_enabled; }
    bool    authenticated() const { return !m_sessionKey.isEmpty(); }
    QString username()      const { return m_username; }
    bool    awaitingAuth()  const { return m_authActive; }

    void setEnabled(bool enabled);

public slots:
    void startAuth();
    void cancelAuth();
    void logout();

signals:
    void enabledChanged();
    void authStateChanged();
    void awaitingAuthChanged();

private slots:
    void onTrackStarted(const Track &track);
    void onPlaybackStateChanged();
    void onPositionChanged();
    void pollAuthSession();

private:
    void setPendingToken(const QString &t);
    void setAuthActive(bool active);
    void finishAuth();
    void scheduleAuthPoll();
    void resetTrackedTrack();
    void startTracking(const Track &track);
    void accumulatePlayTime();
    void maybeScrobble();
    void maybeSendNowPlaying();
    void processScrobbleQueue();
    void scheduleScrobbleRetry();
    void invalidateSession(const QString &expectedKey);
    void restoreScrobbleQueue();
    void saveScrobbleQueue();

    QString signature(const QMap<QString, QString> &params) const;
    QNetworkReply* postSigned(QMap<QString, QString> params);
    QNetworkReply* getSigned(QMap<QString, QString> params);

    PlayerBackend         *m_backend       = nullptr;
    Settings              *m_settings      = nullptr;
    QNetworkAccessManager *m_nam           = nullptr;
    QTimer                *m_authPollTimer = nullptr;
    QTimer                *m_nowPlayingRetryTimer = nullptr;
    QTimer                *m_scrobbleRetryTimer = nullptr;
    QNetworkReply         *m_authReply = nullptr;
    QElapsedTimer          m_authElapsed;
    quint64                m_authGeneration = 0;
    bool                   m_authActive = false;

    QString m_sessionKey;
    QString m_username;
    QString m_pendingToken;

    bool m_enabled = false;

    Track   m_trackedTrack;
    QString m_trackedOwner;
    qint64  m_trackedStartedAtUnix = 0;
    qint64  m_accumulatedMs        = 0;
    QElapsedTimer m_playTimer;
    quint64 m_playbackGeneration = 0;
    bool    m_isPlaying            = false;
    bool    m_nowPlayingSent       = false;
    bool    m_nowPlayingInFlight   = false;
    bool    m_scrobbleQueued       = false;

    struct PendingScrobble {
        quint64 id = 0;
        Track track;
        QString owner;
        qint64 startedAtUnix = 0;
        int retryCount = 0;
    };
    QList<PendingScrobble> m_pendingScrobbles;
    quint64 m_nextScrobbleId = 0;
    quint64 m_scrobbleInFlightId = 0;
};
