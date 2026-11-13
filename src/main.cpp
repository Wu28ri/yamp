#include "AppController.h"
#include "ArtistImageLoader.h"
#include "CoverImageProvider.h"
#include "LastFmScrobbler.h"
#include "PlayerBackend.h"
#include "Settings.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMetaObject>
#include <QMouseEvent>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

#ifndef YAMP_VERSION
#define YAMP_VERSION "0.2"
#endif

namespace {

class MouseNavFilter final : public QObject {
public:
    explicit MouseNavFilter(QObject *window)
        : QObject(window), m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != m_window || !event) return false;
        const QEvent::Type t = event->type();
        if (t != QEvent::MouseButtonPress
            && t != QEvent::MouseButtonDblClick
            && t != QEvent::MouseButtonRelease) {
            return false;
        }
        auto *me = static_cast<QMouseEvent *>(event);
        const Qt::MouseButton btn = me->button();
        if (btn != Qt::BackButton && btn != Qt::ForwardButton) {
            return false;
        }
        if (t == QEvent::MouseButtonPress) {
            QMetaObject::invokeMethod(
                m_window,
                btn == Qt::BackButton ? "goBack" : "goForward");
        }
        return true;
    }

private:
    QObject *m_window = nullptr;
};

} // namespace

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

    QList<QUrl> cliUrls;
    QStringList cliUrlStrings;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QLatin1Char('-'))) continue;
        const QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (url.isValid()) {
            cliUrls.append(url);
            cliUrlStrings.append(url.toString());
        }
    }

    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.isConnected() && !bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.yamp"))) {
            if (!cliUrlStrings.isEmpty()) {
                QDBusMessage openMsg = QDBusMessage::createMethodCall(
                    QStringLiteral("org.mpris.MediaPlayer2.yamp"),
                    QStringLiteral("/org/mpris/MediaPlayer2"),
                    QStringLiteral("org.yamp.Instance"),
                    QStringLiteral("OpenUrls"));
                openMsg << cliUrlStrings;
                const QDBusMessage reply = bus.call(openMsg, QDBus::Block, 2000);
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    for (const QString &u : std::as_const(cliUrlStrings)) {
                        QDBusMessage uriMsg = QDBusMessage::createMethodCall(
                            QStringLiteral("org.mpris.MediaPlayer2.yamp"),
                            QStringLiteral("/org/mpris/MediaPlayer2"),
                            QStringLiteral("org.mpris.MediaPlayer2.Player"),
                            QStringLiteral("OpenUri"));
                        uriMsg << u;
                        bus.call(uriMsg, QDBus::Block, 1000);
                    }
                    QDBusMessage raiseMsg = QDBusMessage::createMethodCall(
                        QStringLiteral("org.mpris.MediaPlayer2.yamp"),
                        QStringLiteral("/org/mpris/MediaPlayer2"),
                        QStringLiteral("org.mpris.MediaPlayer2"),
                        QStringLiteral("Raise"));
                    bus.call(raiseMsg, QDBus::Block, 1000);
                }
            } else {
                QDBusMessage raiseMsg = QDBusMessage::createMethodCall(
                    QStringLiteral("org.mpris.MediaPlayer2.yamp"),
                    QStringLiteral("/org/mpris/MediaPlayer2"),
                    QStringLiteral("org.mpris.MediaPlayer2"),
                    QStringLiteral("Raise"));
                bus.call(raiseMsg, QDBus::Block, 2000);
            }
            return 0;
        }
    }

    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    PlayerBackend backend;
    if (!cliUrls.isEmpty()) {
        backend.setStartupUrls(cliUrls);
    }
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
        const QList<QObject *> roots = engine.rootObjects();
        if (!roots.isEmpty() && roots.first()) {
            roots.first()->installEventFilter(new MouseNavFilter(roots.first()));
        }

        exitCode = app.exec();

        controller.detachCoverProvider();
        artistImages.detachProvider();
    }

    return exitCode;
}
