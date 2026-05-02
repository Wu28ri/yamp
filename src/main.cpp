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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("playerBackend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), &settings);

    QObject::connect(&settings, &Settings::requestRescanDatabase, &backend,
                     [&backend](const QStringList &folders) { backend.syncWithFolders(folders); });

    QObject::connect(&settings, &Settings::requestRemoveFolder,
                     &backend, &PlayerBackend::removeFolder);

    QObject::connect(&settings, &Settings::requestClearDatabase,
                     &backend, &PlayerBackend::clearLibrary);

    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &app,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider);
    engine.loadFromModule(QStringLiteral("yamp"), QStringLiteral("Main"));

    return app.exec();
}