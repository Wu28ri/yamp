#include "CoverImageProvider.h"
#include "PlayerBackend.h"
#include "settings.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

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

    auto *positionSaveTimer = new QTimer(&app);
    positionSaveTimer->setSingleShot(true);
    positionSaveTimer->setInterval(2000);
    QObject::connect(positionSaveTimer, &QTimer::timeout, &settings, [&]() {
        if (!backend.currentPath().isEmpty()) {
            settings.setLastTrackPath(backend.currentPath());
            settings.setLastTrackPosition(backend.position());
        }
    });
    QObject::connect(&backend, &PlayerBackend::positionChanged, positionSaveTimer,
                     [positionSaveTimer]() {
                         if (!positionSaveTimer->isActive()) positionSaveTimer->start();
                     });
    QObject::connect(&backend, &PlayerBackend::metadataChanged, &settings, [&]() {
        if (!backend.currentPath().isEmpty()) {
            settings.setLastTrackPath(backend.currentPath());
        }
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &settings, [&]() {
        if (!backend.currentPath().isEmpty()) {
            settings.setLastTrackPath(backend.currentPath());
            settings.setLastTrackPosition(backend.position());
        }
    });

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

    QObject::connect(&settings, &Settings::coverMaxEdgeChanged, &settings, [coverProvider, &settings]() {
        coverProvider->setMaxEdge(settings.coverMaxEdge());
    });
    QObject::connect(&settings, &Settings::coverSourceBudgetMbChanged, &settings, [coverProvider, &settings]() {
        coverProvider->setSourceBudgetKb(settings.coverSourceBudgetMb() * 1024);
    });
    QObject::connect(&settings, &Settings::coverScaledBudgetMbChanged, &settings, [coverProvider, &settings]() {
        coverProvider->setScaledBudgetKb(settings.coverScaledBudgetMb() * 1024);
    });

    engine.loadFromModule(QStringLiteral("yamp"), QStringLiteral("Main"));

    return app.exec();
}