#pragma once

#include "Track.h"

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
    Q_PROPERTY(QString status         READ status        NOTIFY statusChanged)
    Q_PROPERTY(bool    awaitingAuth   READ awaitingAuth  NOTIFY awaitingAuthChanged)

public:
    explicit LastFmScrobbler(PlayerBackend *backend, Settings *settings,
                             QObject *parent = nullptr);

    bool    enabled()       const { return m_enabled; }
    bool    authenticated() const { return !m_sessionKey.isEmpty(); }
    QString username()      const { return m_username; }
    QString status()        const { return m_status; }
    bool    awaitingAuth()  const { return !m_pendingToken.isEmpty(); }

    void setEnabled(bool enabled);

public slots:
    void startAuth();
    void cancelAuth();
    void logout();

signals:
    void enabledChanged();
    void authStateChanged();
    void statusChanged();
    void awaitingAuthChanged();

private slots:
    void onMetadataChanged();
    void onPlaybackStateChanged();
    void onPositionChanged();
    void pollAuthSession();

private:
    void setStatus(const QString &s);
    void setPendingToken(const QString &t);
    void resetTrackedTrack();
    void accumulatePlayTime();
    void maybeScrobble();
    void sendNowPlaying(const Track &t);
    void sendScrobble(const Track &t, qint64 startedAtUnix);

    QString signature(const QMap<QString, QString> &params) const;
    QNetworkReply* postSigned(QMap<QString, QString> params);
    QNetworkReply* getSigned(QMap<QString, QString> params);

    PlayerBackend         *m_backend       = nullptr;
    Settings              *m_settings      = nullptr;
    QNetworkAccessManager *m_nam           = nullptr;
    QTimer                *m_authPollTimer = nullptr;
    int                    m_authPollsLeft = 0;

    QString m_sessionKey;
    QString m_username;
    QString m_pendingToken;
    QString m_status;

    bool m_enabled = false;

    Track   m_trackedTrack;
    qint64  m_trackedStartedAtUnix = 0;
    qint64  m_accumulatedMs        = 0;
    qint64  m_lastPlayingWallMs    = 0;
    bool    m_isPlaying            = false;
    bool    m_nowPlayingSent       = false;
    bool    m_scrobbled            = false;
};
