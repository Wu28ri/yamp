#pragma once

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtDBus/QDBusObjectPath>

class PlayerBackend;

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool        CanQuit             READ canQuit             CONSTANT)
    Q_PROPERTY(bool        CanRaise            READ canRaise            CONSTANT)
    Q_PROPERTY(bool        HasTrackList        READ hasTrackList        CONSTANT)
    Q_PROPERTY(QString     Identity            READ identity            CONSTANT)
    Q_PROPERTY(QString     DesktopEntry        READ desktopEntry        CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes  READ supportedMimeTypes  CONSTANT)

public:
    explicit MprisRootAdaptor(PlayerBackend *backend);

    bool        canQuit()             const { return true; }
    bool        canRaise()            const { return true; }
    bool        hasTrackList()        const { return false; }
    QString     identity()            const { return QStringLiteral("YAMP Music Player"); }
    QString     desktopEntry()        const { return QStringLiteral("yamp"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes()  const { return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac")}; }

public slots:
    void Raise() {}
    void Quit() { qApp->quit(); }
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString     PlaybackStatus READ playbackStatus NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap Metadata       READ metadata       NOTIFY metadataChanged)
    Q_PROPERTY(qlonglong   Position       READ position       NOTIFY positionChanged)
    Q_PROPERTY(double      Volume         READ volume         WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double      Rate           READ rate           CONSTANT)
    Q_PROPERTY(double      MinimumRate    READ rate           CONSTANT)
    Q_PROPERTY(double      MaximumRate    READ rate           CONSTANT)
    Q_PROPERTY(bool        CanGoNext      READ canControl     CONSTANT)
    Q_PROPERTY(bool        CanGoPrevious  READ canControl     CONSTANT)
    Q_PROPERTY(bool        CanPlay        READ canControl     CONSTANT)
    Q_PROPERTY(bool        CanPause       READ canControl     CONSTANT)
    Q_PROPERTY(bool        CanSeek        READ canControl     CONSTANT)
    Q_PROPERTY(bool        CanControl     READ canControl     CONSTANT)

public:
    explicit MprisPlayerAdaptor(PlayerBackend *backend);

    QString     playbackStatus() const;
    QVariantMap metadata()       const;
    qlonglong   position()       const;
    double      volume()         const;
    double      rate()           const { return 1.0; }
    bool        canControl()     const { return true; }

    void setVolume(double v);

public slots:
    void Next()      { emit nextRequested(); }
    void Previous()  { emit previousRequested(); }
    void Pause()     { emit pauseRequested(); }
    void PlayPause() { emit playPauseRequested(); }
    void Stop()      { emit stopRequested(); }
    void Play()      { emit playRequested(); }
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);

signals:
    void nextRequested();
    void previousRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void playRequested();
    void Seeked(qlonglong positionUs);

    void statusChanged();
    void metadataChanged();
    void positionChanged();
    void volumeChanged();

private:
    void emitProperties(const QVariantMap &props);
    void scheduleMetadataPush();

    PlayerBackend *m_backend;
    QTimer *m_metadataTimer = nullptr;
};
