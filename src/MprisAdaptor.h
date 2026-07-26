#pragma once

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QVariantMap>
#include <QtDBus/QDBusObjectPath>

class PlayerBackend;
class QTimer;

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
    bool        canRaise()            const { return false; }
    bool        hasTrackList()        const { return false; }
    QString     identity()            const { return QStringLiteral("YAMP Music Player"); }
    QString     desktopEntry()        const { return QStringLiteral("yamp"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes()  const {
        return {
            QStringLiteral("audio/mpeg"),
            QStringLiteral("audio/flac"),
            QStringLiteral("audio/x-flac"),
            QStringLiteral("audio/mp4"),
            QStringLiteral("audio/x-m4a"),
            QStringLiteral("audio/aac"),
            QStringLiteral("audio/ogg"),
            QStringLiteral("audio/x-vorbis+ogg"),
            QStringLiteral("audio/opus"),
            QStringLiteral("audio/x-opus+ogg"),
            QStringLiteral("audio/wav"),
            QStringLiteral("audio/x-wav"),
            QStringLiteral("audio/x-ms-wma"),
            QStringLiteral("audio/aiff"),
            QStringLiteral("audio/x-aiff"),
            QStringLiteral("audio/x-ape")
        };
    }

public slots:
    void Quit() { qApp->quit(); }
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString     PlaybackStatus READ playbackStatus NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap Metadata       READ metadata       NOTIFY metadataChanged)
    Q_PROPERTY(qlonglong   Position       READ position       NOTIFY positionChanged)
    Q_PROPERTY(double      Volume         READ volume         WRITE setVolume  NOTIFY volumeChanged)
    Q_PROPERTY(bool        Shuffle        READ shuffle        WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(double      Rate           READ rate           CONSTANT)
    Q_PROPERTY(double      MinimumRate    READ minimumRate    CONSTANT)
    Q_PROPERTY(double      MaximumRate    READ maximumRate    CONSTANT)
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
    bool        shuffle()        const;
    double      rate()           const { return 1.0; }
    double      minimumRate()    const { return 1.0; }
    double      maximumRate()    const { return 1.0; }
    bool        canControl()     const { return true; }

    void setVolume(double v);
    void setShuffle(bool s);

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
    void shuffleChanged();

private:
    void emitProperties(const QVariantMap &props);
    void scheduleMetadataPush();
    void invalidateMetadataCache();
    QString currentTrackIdPath() const;

    PlayerBackend *m_backend;
    QTimer *m_metadataTimer = nullptr;
    mutable QString     m_cachedArtist;
    mutable QStringList m_cachedArtistList;
    mutable QVariantMap m_cachedMetadata;
    mutable bool        m_metadataDirty = true;
    mutable QString     m_lastTrackIdPath;
    mutable quint64     m_trackIdCounter = 0;
};
