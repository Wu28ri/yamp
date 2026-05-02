#pragma once
#include <QObject>
#include <QStringList>
#include <QSettings>
#include <QUrl>

class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList musicFolders READ musicFolders NOTIFY musicFoldersChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    QStringList musicFolders() const;

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

private:
    QStringList m_folders;
    QSettings m_settings;
    void loadSettings();
    void saveSettings();
};