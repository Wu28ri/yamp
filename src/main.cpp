#include "AppController.h"
#include "ArtistImageLoader.h"
#include "CoverImageProvider.h"
#include "LastFmScrobbler.h"
#include "PlayerBackend.h"
#include "Settings.h"

#include <QApplication>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

#ifndef YAMP_VERSION
#define YAMP_VERSION "0.2"
#endif

int main(int argc, char *argv[]) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    QCoreApplication::setApplicationName(QStringLiteral("yamp"));
    QCoreApplication::setOrganizationName(QStringLiteral("yamp"));
    QCoreApplication::setApplicationVersion(QStringLiteral(YAMP_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("YAMP"));
    QGuiApplication::setDesktopFileName(QStringLiteral("yamp"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("yamp")));

    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    PlayerBackend backend;
    Settings      settings;

    auto *coverProvider  = new CoverImageProvider;
    auto *artistProvider = new ArtistImageProvider;

    AppController controller(&backend, &settings, coverProvider);
    controller.applyInitialSettings();

    LastFmScrobbler   scrobbler(&backend, &settings);
    ArtistImageLoader artistImages(&settings, artistProvider);

    int exitCode = 0;
    {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("playerBackend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),   &settings);
        engine.rootContext()->setContextProperty(QStringLiteral("lastfm"),        &scrobbler);
        engine.rootContext()->setContextProperty(QStringLiteral("artistImages"),  &artistImages);

        engine.addImageProvider(QStringLiteral("cover"),  coverProvider);
        engine.addImageProvider(QStringLiteral("artist"), artistProvider);

        controller.wireSignals();

        QObject::connect(&backend, &PlayerBackend::libraryCleared, &engine,
                         [coverProvider, &artistImages]() {
            coverProvider->clearCache();
            artistImages.clearCache();
        });

        QObject::connect(&engine,
                         &QQmlApplicationEngine::objectCreationFailed,
                         &app,
                         []() { QCoreApplication::exit(-1); },
                         Qt::QueuedConnection);

        engine.loadFromModule(QStringLiteral("yamp"), QStringLiteral("Main"));

        exitCode = app.exec();

        controller.detachCoverProvider();
        artistImages.detachProvider();
    }

    return exitCode;
}
