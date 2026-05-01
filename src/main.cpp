#include "CoverImageProvider.h"
#include "PlayerBackend.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    QGuiApplication::setApplicationName(QStringLiteral("yamp"));
    QGuiApplication::setOrganizationName(QStringLiteral("yamp"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("YAMP"));
    QGuiApplication::setDesktopFileName(QStringLiteral("yamp"));

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    PlayerBackend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("playerBackend"), &backend);

    QObject::connect(&engine,
                     &QQmlApplicationEngine::objectCreationFailed,
                     &app,
                     []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.addImageProvider(QStringLiteral("cover"), new CoverImageProvider);
    engine.loadFromModule(QStringLiteral("yamp"), QStringLiteral("Main"));

    return app.exec();
}
