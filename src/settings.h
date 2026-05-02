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

    void setVolume(qreal v);
    void setShuffle(bool s);
    void setLastTrackPath(const QString &p);
    void setLastTrackPosition(qint64 ms);
    void setSidebarWidth(int w);
    void setQueuePanelWidth(int w);
    void setQueuePanelOpen(bool open);

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

    void loadSettings();
    void saveFolders();
};