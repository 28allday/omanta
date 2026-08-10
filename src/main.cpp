// omanta — a file manager for Omarchy.
//
// Single process, many windows, D-Bus activated: the same shape as Nautilus,
// because anything else changes how the desktop's keybindings and "reveal in
// file manager" buttons behave.

#include "Application.h"
#include "DBusAdaptors.h"
#include "IconImageProvider.h"
#include "Platform.h"
#include "SystemTheme.h"
#include "ThumbnailProvider.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

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
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omanta"));
    app.setApplicationDisplayName(QStringLiteral("Files"));
    app.setOrganizationDomain(QStringLiteral("omarchy.org"));
    app.setDesktopFileName(QStringLiteral("omanta"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Browse files."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption newWindowOption(QStringList{ QStringLiteral("n"), QStringLiteral("new-window") },
                                       QStringLiteral("Open a new window."));
    QCommandLineOption selectOption(QStringLiteral("select"),
                                    QStringLiteral("Open the parent folder and select <path>."),
                                    QStringLiteral("path"));
    parser.addOption(newWindowOption);
    parser.addOption(selectOption);
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

    if (!isPrimary && session.isConnected()) {
        if (forwardToRunningInstance(paths, parser.isSet(newWindowOption), selectPath))
            return 0;
    }

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
        // so both can be installed side by side while omanta is on trial.
        if (session.registerService(QLatin1String(kFileManager1Name))) {
            new FileManager1Adaptor(&application);
            session.registerObject(QLatin1String(kFileManager1Path), &application);
        } else {
            std::fprintf(stderr,
                         "omanta: org.freedesktop.FileManager1 is already owned "
                         "(Nautilus running?) — not claiming it\n");
        }
    }

    // Windows are created rather than loaded, so the same code path serves the
    // first launch and every later D-Bus request.
    if (!selectPath.isEmpty())
        application.showItems({ selectPath });
    else
        application.openPaths(paths, parser.isSet(newWindowOption));

    if (application.windowCount() == 0)
        return 1;

    return app.exec();
}
