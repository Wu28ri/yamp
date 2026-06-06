#pragma once

#include "AlbumModel.h"
#include "ArtistModel.h"
#include "LibraryWatcher.h"
#include "PaVolumeController.h"
#include "QueueModel.h"
#include "TrackModel.h"
#include "TrackQueue.h"

#include <QAbstractItemModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

struct mpv_handle;
struct mpv_event_property;
struct mpv_event_end_file;

class ScanSession;

class PlayerBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTitle  READ currentTitle  NOTIFY metadataChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY metadataChanged)
    Q_PROPERTY(QString currentAlbum  READ currentAlbum  NOTIFY metadataChanged)
    Q_PROPERTY(QString currentPath   READ currentPath   NOTIFY metadataChanged)
    Q_PROPERTY(QString currentTechInfo READ currentTechInfo NOTIFY metadataChanged)

    Q_PROPERTY(bool    isMuted   READ isMuted   WRITE setMuted   NOTIFY mutedChanged)
    Q_PROPERTY(qreal   volume    READ volume    WRITE setVolume  NOTIFY volumeChanged)
    Q_PROPERTY(bool    shuffle   READ shuffle   WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(bool    isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64  position  READ position  WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration  READ duration  NOTIFY durationChanged)

    Q_PROPERTY(bool    bitPerfect           READ bitPerfect           WRITE setBitPerfect    NOTIFY bitPerfectChanged)
    Q_PROPERTY(QString audioDevice          READ audioDevice          WRITE setAudioDevice   NOTIFY audioDeviceChanged)
    Q_PROPERTY(bool    softwareVolume       READ softwareVolume       WRITE setSoftwareVolume NOTIFY softwareVolumeChanged)
    Q_PROPERTY(bool    volumeControllable   READ volumeControllable   NOTIFY volumeControllableChanged)

    Q_PROPERTY(int currentIndex         READ currentIndex         NOTIFY currentIndexChanged)
    Q_PROPERTY(int currentQueuePosition READ currentQueuePosition NOTIFY currentQueuePositionChanged)

    Q_PROPERTY(QAbstractItemModel* trackModel  READ trackModel  CONSTANT)
    Q_PROPERTY(QAbstractItemModel* albumModel  READ albumModel  CONSTANT)
    Q_PROPERTY(QAbstractItemModel* artistModel READ artistModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* queueModel  READ queueModel  CONSTANT)

    Q_PROPERTY(bool scanInProgress READ scanInProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int  scanProgress   READ scanProgress   NOTIFY scanProgressChanged)
    Q_PROPERTY(int  scanTotal      READ scanTotal      NOTIFY scanProgressChanged)

public:
    explicit PlayerBackend(QObject *parent = nullptr);
    ~PlayerBackend() override;

    enum ReplayGainMode {
        RgModeTrack = 0,
        RgModeAlbum = 1,
    };
    Q_ENUM(ReplayGainMode)

    QString currentTitle()    const { return m_currentTitle;  }
    QString currentArtist()   const { return m_currentArtist; }
    QString currentAlbum()    const { return m_currentAlbum;  }
    QString currentPath()     const { return m_currentPath;   }
    QString currentTechInfo() const { return m_currentTechInfo; }
    QString currentCoverPath() const { return m_currentCoverPath; }
    const Track& currentTrack() const { return m_currentTrack; }

    bool   isMuted()  const;
    qreal  volume()   const;
    bool   shuffle()  const { return m_queue.isShuffle();      }
    bool   isPlaying() const { return m_hasFile && !m_paused; }
    qint64 position() const;
    qint64 duration() const { return m_durationMs; }

    QString audioDevice()          const { return m_audioDevice; }
    bool    softwareVolume()       const { return m_softwareVolume; }
    bool    bitPerfect()           const { return m_bitPerfectEnabled; }
    bool    volumeControllable()   const { return !m_bitPerfectEnabled || m_softwareVolume; }

    int currentIndex()         const { return m_currentIndex; }
    int currentQueuePosition() const { return m_queue.currentPosition(); }

    QAbstractItemModel* trackModel()  const { return m_trackModel; }
    QAbstractItemModel* albumModel()  const { return m_albumModel; }
    QAbstractItemModel* artistModel() const { return m_artistModel; }
    QAbstractItemModel* queueModel()  const { return m_queueModel; }

    bool scanInProgress() const { return !m_scanProgresses.isEmpty(); }
    int  scanProgress()   const { return m_scanProgressCached; }
    int  scanTotal()      const { return m_scanTotalCached; }

    void setMuted(bool muted);
    void setVolume(qreal v);
    void setShuffle(bool enabled);
    void setPosition(qint64 ms);

    void setAudioDevice(const QString &device);
    void setSoftwareVolume(bool enabled);
    void setBitPerfect(bool enabled);

    void setReplayGainEnabled(bool enabled);
    void setReplayGainMode(int mode);
    void setReplayGainPreampDb(qreal db);
    void setReplayGainClipProtect(bool enabled);

    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void scanFolder(const QUrl &folderUrl);
    Q_INVOKABLE void playMusic(const QString &filePath);
    Q_INVOKABLE void playFromQueue(int position);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrevious();
    Q_INVOKABLE void searchTracks(const QString &query);
    Q_INVOKABLE void searchAlbums(const QString &query);
    Q_INVOKABLE void searchArtists(const QString &query);
    Q_INVOKABLE void filterByAlbum(const QString &albumName, const QString &artistName = QString());
    Q_INVOKABLE void filterByArtist(const QString &artistName);
    Q_INVOKABLE void sortTracks(int column, bool ascending = true);
    Q_INVOKABLE int  getRowForPath(const QString &path);
    Q_INVOKABLE void addPlayNext(const QString &path);
    Q_INVOKABLE void openInFileManager(const QString &path);
    Q_INVOKABLE QVariantMap trackContextForPath(const QString &path);
    Q_INVOKABLE QVariantList listHardwareDevices();
    Q_INVOKABLE bool         lockDeviceToZeroDb();

    void clearLibrary();
    void removeFolder(const QString &folder);
    void syncWithFolders(const QStringList &folders);

signals:
    void metadataChanged();
    void mutedChanged();
    void volumeChanged();
    void shuffleChanged();
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void currentIndexChanged();
    void currentQueuePositionChanged();
    void scanProgressChanged();
    void audioDeviceChanged();
    void softwareVolumeChanged();
    void volumeControllableChanged();
    void bitPerfectChanged();

private:
    void initDatabase();
    void loadTrack(const Track &track);
    void rebuildQueueFromCurrentFilter();
    void refreshAlbumModel();
    void refreshArtistModel();
    void refreshAllModels();
    void resetPlaybackState();
    void recomputeScanTotals();
    void applyFilter();
    QString combinedFilter() const;
    QList<Track> queryTracks(const QString &whereClause = {}, const QString &orderBy = {});
    static QString coverCacheDir();
    static QString coverPathForHash(const QByteArray &hash, const QString &ext);
    static bool writeCoverAtomic(const QString &targetPath, const QByteArray &data);
    static void pruneCoverCache(int keepCount);
    void setupMpris();
    void applyReplayGainSettings();
    void applyAudioDeviceToMpv();
    void applyMpvVolume();
    void initMpv();
    void shutdownMpv();
    static void mpvWakeupCallback(void *ctx);
    Q_INVOKABLE void processMpvEvents();
    void handleMpvPropertyChange(mpv_event_property *prop);
    void handleMpvEndFile(mpv_event_end_file *ev);

    mpv_handle *m_mpv = nullptr;
    QTimer     *m_positionPollTimer = nullptr;
    bool        m_paused    = true;
    bool        m_hasFile   = false;
    qint64      m_lastPositionMs = -1;
    qint64      m_durationMs     = 0;

    PaVolumeController *m_paVolume    = nullptr;
    TrackModel         *m_trackModel  = nullptr;
    AlbumModel     *m_albumModel    = nullptr;
    ArtistModel    *m_artistModel   = nullptr;
    QueueModel     *m_queueModel    = nullptr;
    LibraryWatcher *m_libraryWatcher = nullptr;
    TrackQueue      m_queue;
    QThreadPool     m_coverPool;

    QString m_currentPath;
    QString m_currentTitle;
    QString m_currentArtist;
    QString m_currentAlbum;
    QString m_currentTechInfo;
    QString m_currentCoverPath;
    QString m_categoryFilter;
    QString m_searchFilter;
    int     m_currentIndex = -1;
    quint64 m_coverGen     = 0;
    int           m_sortColumn = -1;
    Qt::SortOrder m_sortOrder  = Qt::AscendingOrder;

    QHash<ScanSession*, QPair<int, int>> m_scanProgresses;
    int     m_scanProgressCached = 0;
    int     m_scanTotalCached    = 0;
    QTimer *m_scanRefreshTimer = nullptr;

    bool    m_rgEnabled      = false;
    int     m_rgMode         = RgModeTrack;
    qreal   m_rgPreampDb     = 0.0;
    bool    m_rgClipProtect  = true;
    QString m_audioDevice    = QStringLiteral("auto");
    bool    m_bitPerfectEnabled = false;
    bool    m_softwareVolume = false;
    qreal   m_swVolume       = 1.0;
    bool    m_swMuted        = false;
    Track   m_currentTrack;
};

