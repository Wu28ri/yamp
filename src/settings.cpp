#include "settings.h"

Settings::Settings(QObject *parent) : QObject(parent), m_settings("YAMP", "Player") {
    loadSettings();
}

QStringList Settings::musicFolders() const {
    return m_folders;
}

void Settings::addFolder(const QUrl &folderUrl) {
    if (folderUrl.isEmpty()) return;

    QString path = folderUrl.toLocalFile();
    if (!m_folders.contains(path)) {
        m_folders.append(path);
        saveSettings();
        emit musicFoldersChanged();
        rescanDatabase();
    }
}

void Settings::removeFolder(int index) {
    if (index >= 0 && index < m_folders.size()) {
        m_folders.removeAt(index);
        saveSettings();
        emit musicFoldersChanged();
    }
}

void Settings::clearDatabase() {
    emit requestClearDatabase();
}

void Settings::rescanDatabase() {
    emit requestRescanDatabase(m_folders);
}

void Settings::loadSettings() {
    m_folders = m_settings.value("musicFolders", QStringList()).toStringList();
}

void Settings::saveSettings() {
    m_settings.setValue("musicFolders", m_folders);
}