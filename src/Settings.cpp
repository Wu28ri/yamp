#include "Settings.h"

#include <QtGlobal>

namespace {

template <typename T>
bool differs(const T &a, const T &b) {
    return a != b;
}

bool differs(qreal a, qreal b) {
    return qAbs(a - b) >= 1e-9;
}

qreal clampPreamp(qreal db) {
    return qBound(Settings::kReplayGainPreampMinDb, db, Settings::kReplayGainPreampMaxDb);
}

}

template <typename T>
void Settings::store(T &member, const T &value, const char *key, void (Settings::*changed)()) {
    if (!differs(member, value)) return;
    member = value;
    m_settings.setValue(QLatin1String(key), QVariant::fromValue(member));
    emit (this->*changed)();
}

Settings::Settings(QObject *parent)
    : QObject(parent), m_settings(QStringLiteral("YAMP"), QStringLiteral("Player")) {
    loadSettings();
}

void Settings::addFolder(const QUrl &folderUrl) {
    if (folderUrl.isEmpty()) return;

    const QString path = folderUrl.toLocalFile();
    if (m_folders.contains(path)) return;

    m_folders.append(path);
    saveFolders();
    emit musicFoldersChanged();
    rescanDatabase();
}

void Settings::removeFolder(int index) {
    if (index < 0 || index >= m_folders.size()) return;
    const QString folder = m_folders.at(index);
    m_folders.removeAt(index);
    saveFolders();
    emit musicFoldersChanged();
    emit requestRemoveFolder(folder);
}

void Settings::clearDatabase()  { emit requestClearDatabase(); }
void Settings::rescanDatabase() { emit requestRescanDatabase(m_folders); }

void Settings::setVolume(qreal v)  { store(m_volume,  v, "volume",  &Settings::volumeChanged); }
void Settings::setShuffle(bool s)  { store(m_shuffle, s, "shuffle", &Settings::shuffleChanged); }

void Settings::setSidebarWidth(int w) {
    if (w <= 0) return;
    store(m_sidebarWidth, w, "sidebarWidth", &Settings::sidebarWidthChanged);
}

void Settings::setQueuePanelWidth(int w) {
    if (w <= 0) return;
    store(m_queuePanelWidth, w, "queuePanelWidth", &Settings::queuePanelWidthChanged);
}

void Settings::setQueuePanelOpen(bool open) {
    store(m_queuePanelOpen, open, "queuePanelOpen", &Settings::queuePanelOpenChanged);
}

void Settings::setCoverMaxEdge(int edge) {
    if (edge <= 0) return;
    store(m_coverMaxEdge, edge, "coverMaxEdge", &Settings::coverMaxEdgeChanged);
}

void Settings::setCoverSourceBudgetMb(int mb) {
    if (mb <= 0) return;
    store(m_coverSourceBudgetMb, mb, "coverSourceBudgetMb", &Settings::coverSourceBudgetMbChanged);
}

void Settings::setCoverScaledBudgetMb(int mb) {
    if (mb <= 0) return;
    store(m_coverScaledBudgetMb, mb, "coverScaledBudgetMb", &Settings::coverScaledBudgetMbChanged);
}

void Settings::setAudioDevice(const QString &device) {
    const QString normalized = device.isEmpty() ? QStringLiteral("auto") : device;
    store(m_audioDevice, normalized, "audioDevice", &Settings::audioDeviceChanged);
}

void Settings::setAudioBitPerfect(bool enabled) {
    store(m_audioBitPerfect, enabled, "audioBitPerfect", &Settings::audioBitPerfectChanged);
}

void Settings::setAudioSoftwareVolume(bool enabled) {
    store(m_audioSoftwareVolume, enabled, "audioSoftwareVolume", &Settings::audioSoftwareVolumeChanged);
}

void Settings::setReplayGainEnabled(bool enabled) {
    store(m_rgEnabled, enabled, "replayGainEnabled", &Settings::replayGainEnabledChanged);
}

void Settings::setReplayGainMode(int mode) {
    if (mode != RgModeTrack && mode != RgModeAlbum) return;
    store(m_rgMode, mode, "replayGainMode", &Settings::replayGainModeChanged);
}

void Settings::setReplayGainPreampDb(qreal db) {
    store(m_rgPreampDb, clampPreamp(db), "replayGainPreampDb", &Settings::replayGainPreampDbChanged);
}

void Settings::setReplayGainClipProtect(bool enabled) {
    store(m_rgClipProtect, enabled, "replayGainClipProtect", &Settings::replayGainClipProtectChanged);
}

void Settings::setLastfmEnabled(bool enabled) {
    store(m_lastfmEnabled, enabled, "lastfmEnabled", &Settings::lastfmEnabledChanged);
}

void Settings::setLastfmSessionKey(const QString &key) {
    store(m_lastfmSessionKey, key, "lastfmSessionKey", &Settings::lastfmSessionKeyChanged);
}

void Settings::setLastfmUsername(const QString &name) {
    store(m_lastfmUsername, name, "lastfmUsername", &Settings::lastfmUsernameChanged);
}

void Settings::loadSettings() {
    const auto loadPositiveInt = [this](const char *key, int fallback) {
        const int v = m_settings.value(QLatin1String(key), fallback).toInt();
        return v > 0 ? v : fallback;
    };

    m_folders             = m_settings.value("musicFolders", QStringList()).toStringList();
    m_volume              = qBound(0.0, m_settings.value("volume", 1.0).toReal(), 1.0);
    m_shuffle             = m_settings.value("shuffle",             false).toBool();
    m_sidebarWidth        = loadPositiveInt("sidebarWidth",        250);
    m_queuePanelWidth     = loadPositiveInt("queuePanelWidth",     320);
    m_queuePanelOpen      = m_settings.value("queuePanelOpen",      false).toBool();
    m_coverMaxEdge        = loadPositiveInt("coverMaxEdge",        384);
    m_coverSourceBudgetMb = loadPositiveInt("coverSourceBudgetMb", 48);
    m_coverScaledBudgetMb = loadPositiveInt("coverScaledBudgetMb", 16);
    m_audioBitPerfect     = m_settings.value("audioBitPerfect",     false).toBool();
    m_audioDevice         = m_settings.value("audioDevice",         QStringLiteral("auto")).toString();
    if (m_audioDevice.isEmpty()) m_audioDevice = QStringLiteral("auto");
    m_audioSoftwareVolume = m_settings.value("audioSoftwareVolume", false).toBool();
    m_rgEnabled           = m_settings.value("replayGainEnabled",   false).toBool();
    m_rgMode              = m_settings.value("replayGainMode",      RgModeTrack).toInt();
    if (m_rgMode != RgModeTrack && m_rgMode != RgModeAlbum) m_rgMode = RgModeTrack;
    m_rgPreampDb          = clampPreamp(m_settings.value("replayGainPreampDb", 0.0).toReal());
    m_rgClipProtect       = m_settings.value("replayGainClipProtect", true).toBool();
    m_lastfmEnabled       = m_settings.value("lastfmEnabled",       false).toBool();
    m_lastfmSessionKey    = m_settings.value("lastfmSessionKey",    QString()).toString();
    m_lastfmUsername      = m_settings.value("lastfmUsername",      QString()).toString();
}

void Settings::saveFolders() {
    m_settings.setValue("musicFolders", m_folders);
}
