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
    m_backend->setBitPerfect(m_settings->audioBitPerfect());
    m_settings->setAudioDevice(m_backend->audioDevice());
    m_settings->setAudioSoftwareVolume(m_backend->softwareVolume());
    m_settings->setAudioBitPerfect(m_backend->bitPerfect());
    m_backend->setShuffle(m_settings->shuffle());
    m_backend->setReplayGainEnabled(m_settings->replayGainEnabled());
    m_backend->setReplayGainMode(m_settings->replayGainMode());
    m_backend->setReplayGainPreampDb(m_settings->replayGainPreampDb());
    m_backend->setReplayGainClipProtect(m_settings->replayGainClipProtect());

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
    connect(b, &PlayerBackend::audioDeviceChanged,    s, [s, b]() { s->setAudioDevice(b->audioDevice()); });
    connect(b, &PlayerBackend::softwareVolumeChanged, s, [s, b]() { s->setAudioSoftwareVolume(b->softwareVolume()); });
    connect(b, &PlayerBackend::bitPerfectChanged,     s, [s, b]() { s->setAudioBitPerfect(b->bitPerfect()); });

    connect(s, &Settings::replayGainEnabledChanged,     b, [s, b]() { b->setReplayGainEnabled(s->replayGainEnabled()); });
    connect(s, &Settings::replayGainModeChanged,        b, [s, b]() { b->setReplayGainMode(s->replayGainMode()); });
    connect(s, &Settings::replayGainPreampDbChanged,    b, [s, b]() { b->setReplayGainPreampDb(s->replayGainPreampDb()); });
    connect(s, &Settings::replayGainClipProtectChanged, b, [s, b]() { b->setReplayGainClipProtect(s->replayGainClipProtect()); });

    connect(s, &Settings::audioDeviceChanged,         b, [s, b]() { b->setAudioDevice(s->audioDevice()); });
    connect(s, &Settings::audioSoftwareVolumeChanged, b, [s, b]() { b->setSoftwareVolume(s->audioSoftwareVolume()); });
    connect(s, &Settings::audioBitPerfectChanged,     b, [s, b]() { b->setBitPerfect(s->audioBitPerfect()); });

    if (m_coverProvider) {
        CoverImageProvider *p = m_coverProvider;
        connect(s, &Settings::coverMaxEdgeChanged, this, [p, s]() {
            p->setMaxEdge(s->coverMaxEdge());
        });
        connect(s, &Settings::coverSourceBudgetMbChanged, this, [p, s]() {
            p->setSourceBudgetKb(s->coverSourceBudgetMb() * 1024);
        });
        connect(s, &Settings::coverScaledBudgetMbChanged, this, [p, s]() {
            p->setScaledBudgetKb(s->coverScaledBudgetMb() * 1024);
        });
    }
}
