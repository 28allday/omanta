// omanta — a file manager for Omarchy.
//
// Single process, many windows, D-Bus activated: the same shape as Nautilus,
// because anything else changes how the desktop's keybindings and "reveal in
// file manager" buttons behave.

#include "Application.h"
#include "DBusAdaptors.h"
#include "IconImageProvider.h"
#include "KineticScroll.h"
#include "Platform.h"
#include "SystemTheme.h"
#include "ThumbnailProvider.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdio>

namespace {

constexpr const char *kServiceName = "org.omarchy.omanta";
constexpr const char *kServicePath = "/org/omarchy/omanta";
constexpr const char *kFileManager1Name = "org.freedesktop.FileManager1";
constexpr const char *kFileManager1Path = "/org/freedesktop/FileManager1";

// Accepts plain paths, file:// URIs, and non-file URIs (trash:///,
// smb://server/share), because callers vary and the .desktop entry
// advertises %U. Local forms normalize to a path; other URIs pass through.
QString toLocation(const QString &argument)
{
    const QUrl url(argument);
    if (url.isLocalFile())
        return url.toLocalFile();
    if (url.scheme().isEmpty())
        return QFileInfo(argument).absoluteFilePath();
    if (url.scheme().length() > 1) // a lone drive-letter-ish scheme is a path
        return argument;
    return {};
}

// Hands this invocation's request to the instance already running and returns
// true if it was accepted. This is what stops a second process existing.
bool forwardToRunningInstance(const QStringList &paths, bool newWindow, const QString &select)
{
    QDBusInterface remote(QLatin1String(kServiceName), QLatin1String(kServicePath),
                          QLatin1String(kServiceName), QDBusConnection::sessionBus());
    if (!remote.isValid())
        return false;

    if (!select.isEmpty()) {
        remote.call(QStringLiteral("SelectPath"), select);
        return true;
    }

    remote.call(QStringLiteral("OpenPaths"), paths, newWindow);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Without an alpha channel in the surface format, a window colour with
    // alpha < 1 (the backgroundOpacity setting) renders on black instead of
    // showing through. Must be set before any QQuickWindow exists.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omanta"));
    app.setApplicationDisplayName(QStringLiteral("Files"));
    app.setOrganizationDomain(QStringLiteral("omarchy.org"));
    app.setDesktopFileName(QStringLiteral("omanta"));
    app.setApplicationVersion(QStringLiteral("0.1.23"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Browse files."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption newWindowOption(QStringList{ QStringLiteral("n"), QStringLiteral("new-window") },
                                       QStringLiteral("Open a new window."));
    QCommandLineOption selectOption(QStringLiteral("select"),
                                    QStringLiteral("Open the parent folder and select <path>."),
                                    QStringLiteral("path"));
    // What a D-Bus .service file runs: no window of its own, the pending
    // FileManager1 call opens whatever the caller asked for.
    QCommandLineOption serviceOption(QStringLiteral("service"),
                                     QStringLiteral("Start without a window and serve D-Bus requests "
                                                    "(for D-Bus activation)."));
    parser.addOption(newWindowOption);
    parser.addOption(selectOption);
    parser.addOption(serviceOption);
    parser.addPositionalArgument(QStringLiteral("paths"),
                                 QStringLiteral("Folders or files to open."),
                                 QStringLiteral("[paths...]"));
    parser.process(app);

    QStringList paths;
    for (const QString &argument : parser.positionalArguments()) {
        const QString path = toLocation(argument);
        if (!path.isEmpty())
            paths.append(path);
    }

    QString selectPath;
    if (parser.isSet(selectOption))
        selectPath = toLocation(parser.value(selectOption));

    // Claiming the name is also how we discover we are the second launch.
    QDBusConnection session = QDBusConnection::sessionBus();
    const bool isPrimary = session.isConnected()
                           && session.registerService(QLatin1String(kServiceName));

    const bool service = parser.isSet(serviceOption);

    if (!isPrimary && session.isConnected()) {
        // Activated while an instance already runs: that instance either owns
        // FileManager1 or will claim it when the name frees up (see below), so
        // there is nothing to forward and nothing to show.
        if (service)
            return 0;
        if (forwardToRunningInstance(paths, parser.isSet(newWindowOption), selectPath))
            return 0;
    }

    // Qt Quick's default distance-field text ignores fontconfig hinting and
    // subpixel antialiasing, so omanta's labels read softer than every GTK
    // app beside it. Native rendering draws them the way the rest of the
    // desktop does.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    // Touchpad scrolls glide on after the fingers lift, as in GTK apps.
    KineticScroll kineticScroll;
    app.installEventFilter(&kineticScroll);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("fileicon"), new IconImageProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);


    Application application(&engine);
    Platform platform;
    SystemTheme systemTheme;

    // Registered under their own URI, never into "Omanta". Mixing manual
    // registrations into a URI owned by qt_add_qml_module is unsupported, and
    // it fails silently: the module's own C++ types stop resolving in QML with
    // nothing more than "X is not a type" to go on.
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &systemTheme);

    if (isPrimary) {
        new OmantaAdaptor(&application);
        session.registerObject(QLatin1String(kServicePath), &application);

        // FileManager1 is owned by whichever file manager got there first. If
        // Nautilus is running, we simply do not offer it — that is deliberate,
        // so both can be installed side by side while omanta is on trial. We
        // do take the name over once its owner goes away, so closing the last
        // Nautilus window hands "open containing folder" back to us without a
        // restart.
        const auto claimFileManager1 = [&session, &application] {
            if (!session.registerService(QLatin1String(kFileManager1Name)))
                return false;
            new FileManager1Adaptor(&application);
            session.registerObject(QLatin1String(kFileManager1Path), &application);
            return true;
        };
        if (!claimFileManager1()) {
            std::fprintf(stderr,
                         "omanta: org.freedesktop.FileManager1 is already owned "
                         "(Nautilus running?) — not claiming it until it is free\n");
            auto *watcher = new QDBusServiceWatcher(QLatin1String(kFileManager1Name), session,
                                                    QDBusServiceWatcher::WatchForUnregistration,
                                                    &application);
            QObject::connect(watcher, &QDBusServiceWatcher::serviceUnregistered, &application,
                             [watcher, claimFileManager1] {
                                 if (claimFileManager1())
                                     watcher->deleteLater();
                             });
        }
    }

    // Windows are created rather than loaded, so the same code path serves the
    // first launch and every later D-Bus request.
    if (!selectPath.isEmpty())
        application.showItems({ selectPath });
    else if (!service || !paths.isEmpty())
        application.openPaths(paths, parser.isSet(newWindowOption));

    if (service) {
        // The bus delivers the request that activated us as soon as the name
        // is ours, so a process still windowless after this long was started
        // for nothing (or asked to show something that does not exist).
        using namespace std::chrono_literals;
        QTimer::singleShot(10s, &application, [&application] {
            if (application.windowCount() == 0)
                QGuiApplication::quit();
        });
    } else if (application.windowCount() == 0) {
        return 1;
    }

    return app.exec();
}
