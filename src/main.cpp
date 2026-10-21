// omanta — a file manager for Omarchy.
//
// Single process, many windows, D-Bus activated: the same shape as Nautilus,
// because anything else changes how the desktop's keybindings and "reveal in
// file manager" buttons behave.

#include "Application.h"
#include "DBusAdaptors.h"
#include "DragCardProvider.h"
#include "FileChooserPortal.h"
#include "IconImageProvider.h"
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

// The file chooser is started by D-Bus whenever an app asks for a dialog,
// which can be while the session is still coming up or already going down:
// no compositor socket yet, or one that has just gone (the other portal
// backends fail at the same moment). Qt treats a missing display as fatal and
// aborts with a core dump. A picker with nowhere to appear simply declines.
// The socket is found the way libwayland finds it; an X display is left for
// Qt to try. A platform that needs no display (offscreen) is never refused.
bool portalHasNoDisplay(int argc, char *argv[])
{
    bool portal = false;
    for (int i = 1; i < argc; ++i)
        portal = portal || qstrcmp(argv[i], "--portal") == 0;
    if (!portal)
        return false;

    const QString platform = qEnvironmentVariable("QT_QPA_PLATFORM");
    if (!platform.isEmpty() && !platform.startsWith(QLatin1String("wayland"))
        && !platform.startsWith(QLatin1String("xcb")))
        return false;

    QString socket = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    if (!socket.startsWith(QLatin1Char('/')))
        socket = qEnvironmentVariable("XDG_RUNTIME_DIR") + QLatin1Char('/') + socket;
    if (QFileInfo::exists(socket))
        return false;
    return qEnvironmentVariable("DISPLAY").isEmpty();
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

// `omanta --portal`: the file chooser backend, started by D-Bus when an app
// asks xdg-desktop-portal for an Open or Save dialog. Its own process, not a
// mode of the browser instance: a picker must come up whether or not a
// browser window is open, a crash in it must not take the user's windows
// down, and it gets its own app id so the compositor can float it.
int runPortal(QGuiApplication &app)
{
    app.setDesktopFileName(QStringLiteral("omanta-picker"));
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    FileChooserPortal::registerTypes();

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("fileicon"), new IconImageProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("dragcard"), new DragCardProvider);

    Application application(&engine);
    Platform platform;
    SystemTheme systemTheme;
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &systemTheme);

    // Opaque whatever the background-opacity preference: the picker sits on
    // top of the app that asked, and that app's text would show through.
    if (QObject *colors = engine.singletonInstance<QObject *>("Omanta", "Colors"))
        colors->setProperty("solid", true);

    FileChooserPortal portal(&engine);
    QDBusConnection session = QDBusConnection::sessionBus();
    // The object first, so the call that activated us finds it the moment
    // the name is ours.
    if (!session.registerVirtualObject(QLatin1String(FileChooserPortal::ObjectPath), &portal,
                                       QDBusConnection::SubPath)) {
        std::fprintf(stderr, "omanta: could not export the file chooser\n");
        return 1;
    }
    if (!session.registerService(QLatin1String(FileChooserPortal::ServiceName))) {
        // Another picker process already serves; nothing for this one to do.
        return 0;
    }

    // Stay warm a while after the last dialog — the next one opens without a
    // process start — then go, so an idle desktop does not carry us.
    using namespace std::chrono_literals;
    QTimer linger;
    linger.setSingleShot(true);
    linger.setInterval(qEnvironmentVariableIsSet("OMANTA_PORTAL_LINGER_MS")
                           ? std::chrono::milliseconds(qEnvironmentVariableIntValue("OMANTA_PORTAL_LINGER_MS"))
                           : std::chrono::milliseconds(5min));
    QObject::connect(&linger, &QTimer::timeout, &app, [&portal] {
        if (portal.pendingCount() == 0)
            QGuiApplication::quit();
    });
    QObject::connect(&portal, &FileChooserPortal::idle, &linger, qOverload<>(&QTimer::start));
    linger.start();

    // Picker windows close themselves; the last one closing must not end the
    // process while it lingers for the next request.
    app.setQuitOnLastWindowClosed(false);
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // Without an alpha channel in the surface format, a window colour with
    // alpha < 1 (the backgroundOpacity setting) renders on black instead of
    // showing through. Must be set before any QQuickWindow exists.
    QQuickWindow::setDefaultAlphaBuffer(true);

    if (portalHasNoDisplay(argc, argv)) {
        std::fputs("omanta: no display to show a file chooser on\n", stderr);
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omanta"));
    app.setApplicationDisplayName(QStringLiteral("Files"));
    app.setOrganizationDomain(QStringLiteral("omarchy.org"));
    app.setDesktopFileName(QStringLiteral("omanta"));
    app.setApplicationVersion(QStringLiteral("0.1.30"));

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
    QCommandLineOption portalOption(QStringLiteral("portal"),
                                    QStringLiteral("Serve the file chooser portal "
                                                   "(started by D-Bus for xdg-desktop-portal)."));
    parser.addOption(portalOption);
    parser.addPositionalArgument(QStringLiteral("paths"),
                                 QStringLiteral("Folders or files to open."),
                                 QStringLiteral("[paths...]"));
    parser.process(app);

    if (parser.isSet(portalOption))
        return runPortal(app);

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

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("fileicon"), new IconImageProvider);
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("dragcard"), new DragCardProvider);


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
