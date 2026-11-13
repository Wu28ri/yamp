#include "AppController.h"

#include "CoverImageProvider.h"
#include "PlayerBackend.h"
#include "Settings.h"

AppController::AppController(PlayerBackend *backend,
                             Settings *settings,
                             CoverImageProvider *coverProvider,
                             QObject *parent)
    : QObject(parent), m_backend(backend), m_settings(settings), m_coverProvider(coverProvider) {}

void AppController::applyInitialSettings() {
    m_backend->setAudioDevice(m_settings->audioDevice());
    m_backend->setSoftwareVolume(m_settings->audioSoftwareVolume());
    m_backend->setVolume(m_settings->volume());
    m_backend->setGaplessPlayback(m_settings->gaplessPlayback());
    m_backend->setBitPerfect(m_settings->audioBitPerfect());
    m_settings->setAudioDevice(m_backend->audioDevice());
    m_settings->setAudioSoftwareVolume(m_backend->softwareVolume());
    m_settings->setAudioBitPerfect(m_backend->bitPerfect());
    m_backend->setShuffle(m_settings->shuffle());
    m_backend->setRepeatMode(m_settings->repeatMode());
    m_backend->setReplayGainEnabled(m_settings->replayGainEnabled());
    m_backend->setReplayGainMode(m_settings->replayGainMode());
    m_backend->setReplayGainPreampDb(m_settings->replayGainPreampDb());
    m_backend->setReplayGainFallbackDb(m_settings->replayGainFallbackDb());
    m_backend->setReplayGainClipProtect(m_settings->replayGainClipProtect());

    if (!m_settings->musicFolders().isEmpty() && m_backend->watchedFolders().isEmpty()) {
        m_backend->syncWithFolders(m_settings->musicFolders());
    }

    if (m_coverProvider) {
        m_coverProvider->setMaxEdge(m_settings->coverMaxEdge());
        m_coverProvider->setSourceBudgetKb(m_settings->coverSourceBudgetMb() * 1024);
        m_coverProvider->setScaledBudgetKb(m_settings->coverScaledBudgetMb() * 1024);
    }
}

void AppController::wireSignals() {
    Settings      *s = m_settings;
    PlayerBackend *b = m_backend;

    connect(s, &Settings::requestRescanDatabase, b,
            [b](const QStringList &folders) { b->syncWithFolders(folders); });
    connect(s, &Settings::requestRemoveFolder, b,
            [b](const QString &folder, const QStringList &remaining) {
                b->removeFolder(folder, remaining);
            });
    connect(s, &Settings::requestClearDatabase, b, &PlayerBackend::clearLibrary);

    connect(b, &PlayerBackend::volumeChanged, s, [s, b]() {
        if (!b->bitPerfect() || b->softwareVolume()) s->setVolume(b->volume());
    });
    connect(b, &PlayerBackend::shuffleChanged,        s, [s, b]() { s->setShuffle(b->shuffle()); });
    connect(b, &PlayerBackend::repeatModeChanged,     s, [s, b]() { s->setRepeatMode(b->repeatMode()); });
    connect(b, &PlayerBackend::audioDeviceChanged,    s, [s, b]() { s->setAudioDevice(b->audioDevice()); });
    connect(b, &PlayerBackend::softwareVolumeChanged, s, [s, b]() { s->setAudioSoftwareVolume(b->softwareVolume()); });
    connect(b, &PlayerBackend::bitPerfectChanged,     s, [s, b]() { s->setAudioBitPerfect(b->bitPerfect()); });

    connect(s, &Settings::replayGainEnabledChanged,     b, [s, b]() { b->setReplayGainEnabled(s->replayGainEnabled()); });
    connect(s, &Settings::replayGainModeChanged,        b, [s, b]() { b->setReplayGainMode(s->replayGainMode()); });
    connect(s, &Settings::replayGainPreampDbChanged,    b, [s, b]() { b->setReplayGainPreampDb(s->replayGainPreampDb()); });
    connect(s, &Settings::replayGainFallbackDbChanged,  b, [s, b]() { b->setReplayGainFallbackDb(s->replayGainFallbackDb()); });
    connect(s, &Settings::replayGainClipProtectChanged, b, [s, b]() { b->setReplayGainClipProtect(s->replayGainClipProtect()); });

    connect(s, &Settings::audioDeviceChanged,         b, [s, b]() { b->setAudioDevice(s->audioDevice()); });
    connect(s, &Settings::audioSoftwareVolumeChanged, b, [s, b]() { b->setSoftwareVolume(s->audioSoftwareVolume()); });
    connect(s, &Settings::audioBitPerfectChanged,     b, [s, b]() { b->setBitPerfect(s->audioBitPerfect()); });
    connect(s, &Settings::gaplessPlaybackChanged,     b, [s, b]() { b->setGaplessPlayback(s->gaplessPlayback()); });

    connect(s, &Settings::coverMaxEdgeChanged, this, [this, s]() {
        if (m_coverProvider) m_coverProvider->setMaxEdge(s->coverMaxEdge());
    });
    connect(s, &Settings::coverSourceBudgetMbChanged, this, [this, s]() {
        if (m_coverProvider) m_coverProvider->setSourceBudgetKb(s->coverSourceBudgetMb() * 1024);
    });
    connect(s, &Settings::coverScaledBudgetMbChanged, this, [this, s]() {
        if (m_coverProvider) m_coverProvider->setScaledBudgetKb(s->coverScaledBudgetMb() * 1024);
    });
}
