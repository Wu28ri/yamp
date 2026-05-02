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
        saveFolders();
        emit musicFoldersChanged();
        rescanDatabase();
    }
}

void Settings::removeFolder(int index) {
    if (index >= 0 && index < m_folders.size()) {
        const QString folder = m_folders.at(index);
        m_folders.removeAt(index);
        saveFolders();
        emit musicFoldersChanged();
        emit requestRemoveFolder(folder);
    }
}

void Settings::clearDatabase() {
    emit requestClearDatabase();
}

void Settings::rescanDatabase() {
    emit requestRescanDatabase(m_folders);
}

void Settings::setVolume(qreal v) {
    if (qFuzzyCompare(m_volume + 1.0, v + 1.0)) return;
    m_volume = v;
    m_settings.setValue("volume", v);
    emit volumeChanged();
}

void Settings::setShuffle(bool s) {
    if (m_shuffle == s) return;
    m_shuffle = s;
    m_settings.setValue("shuffle", s);
    emit shuffleChanged();
}

void Settings::setLastTrackPath(const QString &p) {
    if (m_lastTrackPath == p) return;
    m_lastTrackPath = p;
    m_settings.setValue("lastTrackPath", p);
    emit lastTrackPathChanged();
}

void Settings::setLastTrackPosition(qint64 ms) {
    if (m_lastTrackPosition == ms) return;
    m_lastTrackPosition = ms;
    m_settings.setValue("lastTrackPosition", ms);
    emit lastTrackPositionChanged();
}

void Settings::setSidebarWidth(int w) {
    if (m_sidebarWidth == w || w <= 0) return;
    m_sidebarWidth = w;
    m_settings.setValue("sidebarWidth", w);
    emit sidebarWidthChanged();
}

void Settings::setQueuePanelWidth(int w) {
    if (m_queuePanelWidth == w || w <= 0) return;
    m_queuePanelWidth = w;
    m_settings.setValue("queuePanelWidth", w);
    emit queuePanelWidthChanged();
}

void Settings::setQueuePanelOpen(bool open) {
    if (m_queuePanelOpen == open) return;
    m_queuePanelOpen = open;
    m_settings.setValue("queuePanelOpen", open);
    emit queuePanelOpenChanged();
}

void Settings::setCoverMaxEdge(int edge) {
    if (edge <= 0 || m_coverMaxEdge == edge) return;
    m_coverMaxEdge = edge;
    m_settings.setValue("coverMaxEdge", edge);
    emit coverMaxEdgeChanged();
}

void Settings::setCoverSourceBudgetMb(int mb) {
    if (mb <= 0 || m_coverSourceBudgetMb == mb) return;
    m_coverSourceBudgetMb = mb;
    m_settings.setValue("coverSourceBudgetMb", mb);
    emit coverSourceBudgetMbChanged();
}

void Settings::setCoverScaledBudgetMb(int mb) {
    if (mb <= 0 || m_coverScaledBudgetMb == mb) return;
    m_coverScaledBudgetMb = mb;
    m_settings.setValue("coverScaledBudgetMb", mb);
    emit coverScaledBudgetMbChanged();
}

void Settings::loadSettings() {
    m_folders             = m_settings.value("musicFolders", QStringList()).toStringList();
    m_volume              = m_settings.value("volume",              1.0).toReal();
    m_shuffle             = m_settings.value("shuffle",             false).toBool();
    m_lastTrackPath       = m_settings.value("lastTrackPath",       QString()).toString();
    m_lastTrackPosition   = m_settings.value("lastTrackPosition",   0).toLongLong();
    m_sidebarWidth        = m_settings.value("sidebarWidth",        250).toInt();
    m_queuePanelWidth     = m_settings.value("queuePanelWidth",     320).toInt();
    m_queuePanelOpen      = m_settings.value("queuePanelOpen",      false).toBool();
    m_coverMaxEdge        = m_settings.value("coverMaxEdge",        384).toInt();
    m_coverSourceBudgetMb = m_settings.value("coverSourceBudgetMb", 48).toInt();
    m_coverScaledBudgetMb = m_settings.value("coverScaledBudgetMb", 16).toInt();
}

void Settings::saveFolders() {
    m_settings.setValue("musicFolders", m_folders);
}