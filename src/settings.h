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
    Q_PROPERTY(QString lastTrackPath     READ lastTrackPath     WRITE setLastTrackPath     NOTIFY lastTrackPathChanged)
    Q_PROPERTY(qint64  lastTrackPosition READ lastTrackPosition WRITE setLastTrackPosition NOTIFY lastTrackPositionChanged)
    Q_PROPERTY(int     sidebarWidth      READ sidebarWidth      WRITE setSidebarWidth      NOTIFY sidebarWidthChanged)
    Q_PROPERTY(int     queuePanelWidth   READ queuePanelWidth   WRITE setQueuePanelWidth   NOTIFY queuePanelWidthChanged)
    Q_PROPERTY(bool    queuePanelOpen    READ queuePanelOpen    WRITE setQueuePanelOpen    NOTIFY queuePanelOpenChanged)
    Q_PROPERTY(int     coverMaxEdge        READ coverMaxEdge        WRITE setCoverMaxEdge        NOTIFY coverMaxEdgeChanged)
    Q_PROPERTY(int     coverSourceBudgetMb READ coverSourceBudgetMb WRITE setCoverSourceBudgetMb NOTIFY coverSourceBudgetMbChanged)
    Q_PROPERTY(int     coverScaledBudgetMb READ coverScaledBudgetMb WRITE setCoverScaledBudgetMb NOTIFY coverScaledBudgetMbChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    QStringList musicFolders() const;

    qreal   volume()            const { return m_volume; }
    bool    shuffle()           const { return m_shuffle; }
    QString lastTrackPath()     const { return m_lastTrackPath; }
    qint64  lastTrackPosition() const { return m_lastTrackPosition; }
    int     sidebarWidth()      const { return m_sidebarWidth; }
    int     queuePanelWidth()   const { return m_queuePanelWidth; }
    bool    queuePanelOpen()    const { return m_queuePanelOpen; }
    int     coverMaxEdge()        const { return m_coverMaxEdge; }
    int     coverSourceBudgetMb() const { return m_coverSourceBudgetMb; }
    int     coverScaledBudgetMb() const { return m_coverScaledBudgetMb; }

    void setVolume(qreal v);
    void setShuffle(bool s);
    void setLastTrackPath(const QString &p);
    void setLastTrackPosition(qint64 ms);
    void setSidebarWidth(int w);
    void setQueuePanelWidth(int w);
    void setQueuePanelOpen(bool open);
    void setCoverMaxEdge(int edge);
    void setCoverSourceBudgetMb(int mb);
    void setCoverScaledBudgetMb(int mb);

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
    void lastTrackPathChanged();
    void lastTrackPositionChanged();
    void sidebarWidthChanged();
    void queuePanelWidthChanged();
    void queuePanelOpenChanged();
    void coverMaxEdgeChanged();
    void coverSourceBudgetMbChanged();
    void coverScaledBudgetMbChanged();

private:
    QStringList m_folders;
    QSettings   m_settings;

    qreal   m_volume            = 1.0;
    bool    m_shuffle           = false;
    QString m_lastTrackPath;
    qint64  m_lastTrackPosition = 0;
    int     m_sidebarWidth      = 250;
    int     m_queuePanelWidth   = 320;
    bool    m_queuePanelOpen    = false;
    int     m_coverMaxEdge        = 384;
    int     m_coverSourceBudgetMb = 48;
    int     m_coverScaledBudgetMb = 16;

    void loadSettings();
    void saveFolders();
};