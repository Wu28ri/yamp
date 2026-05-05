#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QUrl>

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList musicFolders READ musicFolders NOTIFY musicFoldersChanged)
    Q_PROPERTY(qreal   volume     READ volume     WRITE setVolume     NOTIFY volumeChanged)
    Q_PROPERTY(bool    shuffle    READ shuffle    WRITE setShuffle    NOTIFY shuffleChanged)
    Q_PROPERTY(int     sidebarWidth      READ sidebarWidth      WRITE setSidebarWidth      NOTIFY sidebarWidthChanged)
    Q_PROPERTY(int     queuePanelWidth   READ queuePanelWidth   WRITE setQueuePanelWidth   NOTIFY queuePanelWidthChanged)
    Q_PROPERTY(bool    queuePanelOpen    READ queuePanelOpen    WRITE setQueuePanelOpen    NOTIFY queuePanelOpenChanged)
    Q_PROPERTY(int     coverMaxEdge        READ coverMaxEdge        WRITE setCoverMaxEdge        NOTIFY coverMaxEdgeChanged)
    Q_PROPERTY(int     coverSourceBudgetMb READ coverSourceBudgetMb WRITE setCoverSourceBudgetMb NOTIFY coverSourceBudgetMbChanged)
    Q_PROPERTY(int     coverScaledBudgetMb READ coverScaledBudgetMb WRITE setCoverScaledBudgetMb NOTIFY coverScaledBudgetMbChanged)
    Q_PROPERTY(bool    replayGainEnabled READ replayGainEnabled WRITE setReplayGainEnabled NOTIFY replayGainEnabledChanged)
    Q_PROPERTY(int     replayGainMode    READ replayGainMode    WRITE setReplayGainMode    NOTIFY replayGainModeChanged)
    Q_PROPERTY(qreal   replayGainPreampDb    READ replayGainPreampDb    WRITE setReplayGainPreampDb    NOTIFY replayGainPreampDbChanged)
    Q_PROPERTY(bool    replayGainClipProtect READ replayGainClipProtect WRITE setReplayGainClipProtect NOTIFY replayGainClipProtectChanged)

public:
    // Mirrors PlayerBackend::ReplayGainMode.
    enum ReplayGainMode { RgModeTrack = 0, RgModeAlbum = 1 };
    Q_ENUM(ReplayGainMode)

    explicit Settings(QObject *parent = nullptr);

    QStringList musicFolders() const;

    qreal   volume()              const { return m_volume; }
    bool    shuffle()             const { return m_shuffle; }
    int     sidebarWidth()        const { return m_sidebarWidth; }
    int     queuePanelWidth()     const { return m_queuePanelWidth; }
    bool    queuePanelOpen()      const { return m_queuePanelOpen; }
    int     coverMaxEdge()        const { return m_coverMaxEdge; }
    int     coverSourceBudgetMb() const { return m_coverSourceBudgetMb; }
    int     coverScaledBudgetMb() const { return m_coverScaledBudgetMb; }
    bool    replayGainEnabled()     const { return m_rgEnabled; }
    int     replayGainMode()        const { return m_rgMode; }
    qreal   replayGainPreampDb()    const { return m_rgPreampDb; }
    bool    replayGainClipProtect() const { return m_rgClipProtect; }

    void setVolume(qreal v);
    void setShuffle(bool s);
    void setSidebarWidth(int w);
    void setQueuePanelWidth(int w);
    void setQueuePanelOpen(bool open);
    void setCoverMaxEdge(int edge);
    void setCoverSourceBudgetMb(int mb);
    void setCoverScaledBudgetMb(int mb);
    void setReplayGainEnabled(bool enabled);
    void setReplayGainMode(int mode);
    void setReplayGainPreampDb(qreal db);
    void setReplayGainClipProtect(bool enabled);

public slots:
    void addFolder(const QUrl &folderUrl);
    void removeFolder(int index);
    void clearDatabase();
    void rescanDatabase();

signals:
    void musicFoldersChanged();
    void requestClearDatabase();
    void requestRescanDatabase(const QStringList &folders);
    void requestRemoveFolder(const QString &folder);
    void volumeChanged();
    void shuffleChanged();
    void sidebarWidthChanged();
    void queuePanelWidthChanged();
    void queuePanelOpenChanged();
    void coverMaxEdgeChanged();
    void coverSourceBudgetMbChanged();
    void coverScaledBudgetMbChanged();
    void replayGainEnabledChanged();
    void replayGainModeChanged();
    void replayGainPreampDbChanged();
    void replayGainClipProtectChanged();

private:
    QStringList m_folders;
    QSettings   m_settings;

    qreal   m_volume              = 1.0;
    bool    m_shuffle             = false;
    int     m_sidebarWidth        = 250;
    int     m_queuePanelWidth     = 320;
    bool    m_queuePanelOpen      = false;
    int     m_coverMaxEdge        = 384;
    int     m_coverSourceBudgetMb = 48;
    int     m_coverScaledBudgetMb = 16;
    bool    m_rgEnabled           = false;
    int     m_rgMode              = RgModeTrack;
    qreal   m_rgPreampDb          = 0.0;
    bool    m_rgClipProtect       = true;

    void loadSettings();
    void saveFolders();
};