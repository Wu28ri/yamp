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

void Settings::setAudioDevice(const QString &device) {
    const QString normalized = device.isEmpty() ? QStringLiteral("auto") : device;
    if (m_audioDevice == normalized) return;
    m_audioDevice = normalized;
    m_settings.setValue("audioDevice", normalized);
    emit audioDeviceChanged();
}

void Settings::setAudioBitPerfect(bool enabled) {
    if (m_audioBitPerfect == enabled) return;
    m_audioBitPerfect = enabled;
    m_settings.setValue("audioBitPerfect", enabled);
    emit audioBitPerfectChanged();
}

void Settings::setAudioSoftwareVolume(bool enabled) {
    if (m_audioSoftwareVolume == enabled) return;
    m_audioSoftwareVolume = enabled;
    m_settings.setValue("audioSoftwareVolume", enabled);
    emit audioSoftwareVolumeChanged();
}

void Settings::setReplayGainEnabled(bool enabled) {
    if (m_rgEnabled == enabled) return;
    m_rgEnabled = enabled;
    m_settings.setValue("replayGainEnabled", enabled);
    emit replayGainEnabledChanged();
}

void Settings::setReplayGainMode(int mode) {
    if (mode != RgModeTrack && mode != RgModeAlbum) return;
    if (m_rgMode == mode) return;
    m_rgMode = mode;
    m_settings.setValue("replayGainMode", mode);
    emit replayGainModeChanged();
}

void Settings::setReplayGainPreampDb(qreal db) {
    if (db < -15.0) db = -15.0;
    if (db >  15.0) db =  15.0;
    if (qFuzzyCompare(m_rgPreampDb + 100.0, db + 100.0)) return;
    m_rgPreampDb = db;
    m_settings.setValue("replayGainPreampDb", db);
    emit replayGainPreampDbChanged();
}

void Settings::setReplayGainClipProtect(bool enabled) {
    if (m_rgClipProtect == enabled) return;
    m_rgClipProtect = enabled;
    m_settings.setValue("replayGainClipProtect", enabled);
    emit replayGainClipProtectChanged();
}

void Settings::setLastfmEnabled(bool enabled) {
    if (m_lastfmEnabled == enabled) return;
    m_lastfmEnabled = enabled;
    m_settings.setValue("lastfmEnabled", enabled);
    emit lastfmEnabledChanged();
}

void Settings::setLastfmSessionKey(const QString &key) {
    if (m_lastfmSessionKey == key) return;
    m_lastfmSessionKey = key;
    m_settings.setValue("lastfmSessionKey", key);
    emit lastfmSessionKeyChanged();
}

void Settings::setLastfmUsername(const QString &name) {
    if (m_lastfmUsername == name) return;
    m_lastfmUsername = name;
    m_settings.setValue("lastfmUsername", name);
    emit lastfmUsernameChanged();
}

void Settings::loadSettings() {
    m_folders             = m_settings.value("musicFolders", QStringList()).toStringList();
    m_volume              = m_settings.value("volume",              1.0).toReal();
    m_shuffle             = m_settings.value("shuffle",             false).toBool();
    m_sidebarWidth        = m_settings.value("sidebarWidth",        250).toInt();
    m_queuePanelWidth     = m_settings.value("queuePanelWidth",     320).toInt();
    m_queuePanelOpen      = m_settings.value("queuePanelOpen",      false).toBool();
    m_coverMaxEdge        = m_settings.value("coverMaxEdge",        384).toInt();
    m_coverSourceBudgetMb = m_settings.value("coverSourceBudgetMb", 48).toInt();
    m_coverScaledBudgetMb = m_settings.value("coverScaledBudgetMb", 16).toInt();
    m_audioBitPerfect     = m_settings.value("audioBitPerfect",     false).toBool();
    m_audioDevice         = m_settings.value("audioDevice",         QStringLiteral("auto")).toString();
    if (m_audioDevice.isEmpty()) m_audioDevice = QStringLiteral("auto");
    m_audioSoftwareVolume = m_settings.value("audioSoftwareVolume", false).toBool();
    m_rgEnabled           = m_settings.value("replayGainEnabled",     false).toBool();
    m_rgMode              = m_settings.value("replayGainMode",        RgModeTrack).toInt();
    if (m_rgMode != RgModeTrack && m_rgMode != RgModeAlbum) m_rgMode = RgModeTrack;
    m_rgPreampDb          = m_settings.value("replayGainPreampDb",    0.0).toReal();
    if (m_rgPreampDb < -15.0) m_rgPreampDb = -15.0;
    if (m_rgPreampDb >  15.0) m_rgPreampDb =  15.0;
    m_rgClipProtect       = m_settings.value("replayGainClipProtect", true).toBool();
    m_lastfmEnabled       = m_settings.value("lastfmEnabled",        false).toBool();
    m_lastfmSessionKey    = m_settings.value("lastfmSessionKey",     QString()).toString();
    m_lastfmUsername      = m_settings.value("lastfmUsername",       QString()).toString();
}

void Settings::saveFolders() {
    m_settings.setValue("musicFolders", m_folders);
}
