#include "CoverImageProvider.h"
#include "PlayerBackend.h"
#include "settings.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName(QStringLiteral("yamp"));
    QGuiApplication::setOrganizationName(QStringLiteral("yamp"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("YAMP"));
    QGuiApplication::setDesktopFileName(QStringLiteral("yamp"));

    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    PlayerBackend backend;
    Settings settings;

    backend.setVolume(settings.volume());
    backend.setShuffle(settings.shuffle());
    backend.setReplayGainEnabled(settings.replayGainEnabled());
    backend.setReplayGainMode(settings.replayGainMode());
    backend.setReplayGainPreampDb(settings.replayGainPreampDb());
    backend.setReplayGainClipProtect(settings.replayGainClipProtect());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("playerBackend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);

    QObject::connect(&settings, &Settings::requestRescanDatabase, &backend,
                     [&backend](const QStringList &folders) { backend.syncWithFolders(folders); });

    QObject::connect(&settings, &Settings::requestRemoveFolder,
                     &backend, &PlayerBackend::removeFolder);

    QObject::connect(&settings, &Settings::requestClearDatabase,
                     &backend, &PlayerBackend::clearLibrary);

    QObject::connect(&backend, &PlayerBackend::volumeChanged, &settings,
                     [&]() { settings.setVolume(backend.volume()); });

    QObject::connect(&backend, &PlayerBackend::shuffleChanged, &settings,
                     [&]() { settings.setShuffle(backend.shuffle()); });

    QObject::connect(&settings, &Settings::replayGainEnabledChanged, &backend,
                     [&]() { backend.setReplayGainEnabled(settings.replayGainEnabled()); });
    QObject::connect(&settings, &Settings::replayGainModeChanged, &backend,
                     [&]() { backend.setReplayGainMode(settings.replayGainMode()); });
    QObject::connect(&settings, &Settings::replayGainPreampDbChanged, &backend,
                     [&]() { backend.setReplayGainPreampDb(settings.replayGainPreampDb()); });
    QObject::connect(&settings, &Settings::replayGainClipProtectChanged, &backend,
                     [&]() { backend.setReplayGainClipProtect(settings.replayGainClipProtect()); });

    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &app,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    auto *coverProvider = new CoverImageProvider;
    coverProvider->setMaxEdge(settings.coverMaxEdge());
    coverProvider->setSourceBudgetKb(settings.coverSourceBudgetMb() * 1024);
    coverProvider->setScaledBudgetKb(settings.coverScaledBudgetMb() * 1024);
    engine.addImageProvider(QStringLiteral("cover"), coverProvider);

    QObject::connect(&settings, &Settings::coverMaxEdgeChanged, &engine, [coverProvider, &settings]() {
        coverProvider->setMaxEdge(settings.coverMaxEdge());
    });
    QObject::connect(&settings, &Settings::coverSourceBudgetMbChanged, &engine, [coverProvider, &settings]() {
        coverProvider->setSourceBudgetKb(settings.coverSourceBudgetMb() * 1024);
    });
    QObject::connect(&settings, &Settings::coverScaledBudgetMbChanged, &engine, [coverProvider, &settings]() {
        coverProvider->setScaledBudgetKb(settings.coverScaledBudgetMb() * 1024);
    });

    engine.loadFromModule(QStringLiteral("yamp"), QStringLiteral("Main"));

    return app.exec();
}