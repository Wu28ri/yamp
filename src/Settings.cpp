#include "Settings.h"

#include <QDir>
#include <QFileInfo>
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

QString normalizeFolderPath(const QString &raw) {
    if (raw.isEmpty()) return {};
    const QFileInfo fi(raw);
    QString p = fi.exists() ? fi.canonicalFilePath() : QDir::cleanPath(fi.absoluteFilePath());
    if (p.isEmpty()) p = QDir::cleanPath(raw);
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

bool isSameOrSubPath(const QString &candidate, const QString &root) {
    if (candidate == root) return true;
    if (root == QLatin1String("/")) return candidate.startsWith(QLatin1Char('/'));
    return candidate.startsWith(root + QLatin1Char('/'));
}

QStringList deduplicateRoots(const QStringList &input) {
    QStringList out;
    for (const QString &raw : input) {
        const QString p = normalizeFolderPath(raw);
        if (p.isEmpty()) continue;
        bool covered = false;
        for (const QString &existing : out) {
            if (isSameOrSubPath(p, existing)) {
                covered = true;
                break;
            }
        }
        if (covered) continue;
        for (int i = out.size() - 1; i >= 0; --i) {
            if (isSameOrSubPath(out.at(i), p))
                out.removeAt(i);
        }
        out.append(p);
    }
    return out;
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

    const QString path = normalizeFolderPath(folderUrl.toLocalFile());
    if (path.isEmpty()) return;
    for (const QString &existing : std::as_const(m_folders)) {
        if (isSameOrSubPath(path, existing)) return;
    }

    for (int i = m_folders.size() - 1; i >= 0; --i) {
        if (isSameOrSubPath(m_folders.at(i), path))
            m_folders.removeAt(i);
    }

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
    emit requestRemoveFolder(folder, m_folders);
}

void Settings::clearDatabase() {
    if (!m_folders.isEmpty()) {
        m_folders.clear();
        saveFolders();
        emit musicFoldersChanged();
    }
    emit requestClearDatabase();
}
void Settings::rescanDatabase() { emit requestRescanDatabase(m_folders); }

void Settings::setVolume(qreal v)  { store(m_volume,  v, "volume",  &Settings::volumeChanged); }
void Settings::setShuffle(bool s)  { store(m_shuffle, s, "shuffle", &Settings::shuffleChanged); }
void Settings::setRepeatMode(int mode) {
    if (mode < 0 || mode > 2) return;
    store(m_repeatMode, mode, "repeatMode", &Settings::repeatModeChanged);
}

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

void Settings::setGaplessPlayback(bool enabled) {
    store(m_gaplessPlayback, enabled, "gaplessPlayback", &Settings::gaplessPlaybackChanged);
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

void Settings::setLastfmArtistImages(bool enabled) {
    store(m_lastfmArtistImages, enabled, "lastfmArtistImages", &Settings::lastfmArtistImagesChanged);
}

void Settings::setLastfmSessionKey(const QString &key) {
    store(m_lastfmSessionKey, key, "lastfmSessionKey", &Settings::lastfmSessionKeyChanged);
}

void Settings::setLastfmUsername(const QString &name) {
    store(m_lastfmUsername, name, "lastfmUsername", &Settings::lastfmUsernameChanged);
}

void Settings::setLastfmPendingQueue(const QByteArray &queue) {
    if (m_lastfmPendingQueue == queue) return;
    m_lastfmPendingQueue = queue;
    m_settings.setValue(QStringLiteral("lastfmPendingQueue"), queue);
}

void Settings::loadSettings() {
    const auto loadPositiveInt = [this](const char *key, int fallback) {
        const int v = m_settings.value(QLatin1String(key), fallback).toInt();
        return v > 0 ? v : fallback;
    };

    m_folders             = deduplicateRoots(m_settings.value("musicFolders", QStringList()).toStringList());
    m_volume              = qBound(0.0, m_settings.value("volume", 1.0).toReal(), 1.0);
    m_shuffle             = m_settings.value("shuffle",             false).toBool();
    m_repeatMode          = qBound(0, m_settings.value("repeatMode", 0).toInt(), 2);
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
    m_gaplessPlayback     = m_settings.value("gaplessPlayback",     true).toBool();
    m_rgEnabled           = m_settings.value("replayGainEnabled",   false).toBool();
    m_rgMode              = m_settings.value("replayGainMode",      RgModeTrack).toInt();
    if (m_rgMode != RgModeTrack && m_rgMode != RgModeAlbum) m_rgMode = RgModeTrack;
    m_rgPreampDb          = clampPreamp(m_settings.value("replayGainPreampDb", 0.0).toReal());
    m_rgClipProtect       = m_settings.value("replayGainClipProtect", true).toBool();
    m_lastfmEnabled       = m_settings.value("lastfmEnabled",       false).toBool();
    m_lastfmArtistImages  = m_settings.value("lastfmArtistImages",  false).toBool();
    m_lastfmSessionKey    = m_settings.value("lastfmSessionKey",    QString()).toString();
    m_lastfmUsername      = m_settings.value("lastfmUsername",      QString()).toString();
    m_lastfmPendingQueue  = m_settings.value("lastfmPendingQueue",  QByteArray()).toByteArray();
}

void Settings::saveFolders() {
    m_settings.setValue("musicFolders", m_folders);
}
