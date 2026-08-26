#include "NavigationHistory.h"
#include "Platform.h"
#include "TestFixture.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

// Pure logic, no filesystem timing — these are the fast tests.
class TestNavigation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NavigationHistory
    void startsEmpty();
    void visitThenBackAndForward();
    void visitingTruncatesTheForwardBranch();
    void revisitingCurrentIsANoOp();
    void clearResetsEverything();

    // Platform
    void resolvesTilde();
    void resolvesRelativePaths();
    void resolvesAbsolutePaths();
    void resolveHandlesEmptyInput();
    void parentPathStopsAtRoot();
    void baseNameHandlesRoot();
    void crumbsStartAtHomeWhenUnderHome();
    void crumbsStartAtRootOtherwise();
    void formatsSizes();

    // Locations (URIs)
    void uriDetectionAndLocality();
    void uriParentStopsAtItsRoot();
    void uriDisplayNames();
    void uriCrumbs();
    void typedUrisPassThroughResolve();
    void fileUrisNormalizeToPaths();

    // Drag and drop plumbing
    void rendersAndParsesUriLists();
    void sameFilesystemAnswersForLocalPaths();

    // Templates (New Document)
    void templatesListsVisibleFilesSorted();
    void templatesMissingDirIsEmpty();

    // MUST run last: it rewires XDG_* for GIO's app-info lookup.
    void activationExtractsOnlyWhenSelfIsDefault();
};

void TestNavigation::startsEmpty()
{
    NavigationHistory history;
    QVERIFY(history.current().isEmpty());
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());
    QVERIFY(history.goBack().isEmpty());
    QVERIFY(history.goForward().isEmpty());
}

void TestNavigation::visitThenBackAndForward()
{
    NavigationHistory history;
    history.visit(QStringLiteral("/one"));
    history.visit(QStringLiteral("/two"));
    history.visit(QStringLiteral("/three"));

    QCOMPARE(history.current(), QStringLiteral("/three"));
    QVERIFY(history.canGoBack());
    QVERIFY(!history.canGoForward());

    QCOMPARE(history.goBack(), QStringLiteral("/two"));
    QVERIFY(history.canGoForward());
    QCOMPARE(history.goBack(), QStringLiteral("/one"));
    QVERIFY(!history.canGoBack());

    QCOMPARE(history.goForward(), QStringLiteral("/two"));
    QCOMPARE(history.goForward(), QStringLiteral("/three"));
    QVERIFY(!history.canGoForward());
}

void TestNavigation::visitingTruncatesTheForwardBranch()
{
    NavigationHistory history;
    history.visit(QStringLiteral("/one"));
    history.visit(QStringLiteral("/two"));
    history.visit(QStringLiteral("/three"));
    history.goBack();
    history.goBack(); // now at /one with /two, /three ahead

    history.visit(QStringLiteral("/elsewhere"));

    QCOMPARE(history.current(), QStringLiteral("/elsewhere"));
    QVERIFY(!history.canGoForward()); // the old branch is gone
    QCOMPARE(history.goBack(), QStringLiteral("/one"));
}

void TestNavigation::revisitingCurrentIsANoOp()
{
    NavigationHistory history;
    history.visit(QStringLiteral("/one"));
    history.visit(QStringLiteral("/one"));
    history.visit(QStringLiteral("/one"));

    // Duplicates would make Back appear to do nothing.
    QVERIFY(!history.canGoBack());
    QCOMPARE(history.current(), QStringLiteral("/one"));

    history.visit(QString()); // empty paths are ignored too
    QCOMPARE(history.current(), QStringLiteral("/one"));
}

void TestNavigation::clearResetsEverything()
{
    NavigationHistory history;
    history.visit(QStringLiteral("/one"));
    history.visit(QStringLiteral("/two"));
    history.clear();

    QVERIFY(history.current().isEmpty());
    QVERIFY(!history.canGoBack());
    QVERIFY(!history.canGoForward());
}

void TestNavigation::resolvesTilde()
{
    Platform platform;
    const QString home = QDir::homePath();

    QCOMPARE(platform.resolvePath(QStringLiteral("~"), QString()), home);
    QCOMPARE(platform.resolvePath(QStringLiteral("~/Documents"), QString()),
             home + QStringLiteral("/Documents"));
    // A tilde that isn't a home reference stays literal.
    QCOMPARE(platform.resolvePath(QStringLiteral("/tmp/~weird"), QString()),
             QStringLiteral("/tmp/~weird"));
}

void TestNavigation::resolvesRelativePaths()
{
    Platform platform;

    QCOMPARE(platform.resolvePath(QStringLiteral("src"), QStringLiteral("/home/user/project")),
             QStringLiteral("/home/user/project/src"));
    QCOMPARE(platform.resolvePath(QStringLiteral("../other"), QStringLiteral("/home/user/project")),
             QStringLiteral("/home/user/other"));
    // Whitespace from a paste should not break navigation.
    QCOMPARE(platform.resolvePath(QStringLiteral("  src  "), QStringLiteral("/home/user")),
             QStringLiteral("/home/user/src"));
}

void TestNavigation::resolvesAbsolutePaths()
{
    Platform platform;
    QCOMPARE(platform.resolvePath(QStringLiteral("/etc/fstab"), QStringLiteral("/home/user")),
             QStringLiteral("/etc/fstab"));
    QCOMPARE(platform.resolvePath(QStringLiteral("/usr//lib/../bin"), QString()),
             QStringLiteral("/usr/bin"));
}

void TestNavigation::resolveHandlesEmptyInput()
{
    Platform platform;
    QVERIFY(platform.resolvePath(QString(), QStringLiteral("/home")).isEmpty());
    QVERIFY(platform.resolvePath(QStringLiteral("   "), QStringLiteral("/home")).isEmpty());
}

void TestNavigation::parentPathStopsAtRoot()
{
    Platform platform;
    QCOMPARE(platform.parentPath(QStringLiteral("/usr/lib")), QStringLiteral("/usr"));
    QCOMPARE(platform.parentPath(QStringLiteral("/usr")), QStringLiteral("/"));
    // Root has no parent — this is what disables the Up button.
    QVERIFY(platform.parentPath(QStringLiteral("/")).isEmpty());
    QVERIFY(platform.parentPath(QString()).isEmpty());
}

void TestNavigation::baseNameHandlesRoot()
{
    Platform platform;
    QCOMPARE(platform.baseName(QStringLiteral("/usr/lib")), QStringLiteral("lib"));
    // Without the special case this would be an empty tab title.
    QCOMPARE(platform.baseName(QStringLiteral("/")), QStringLiteral("/"));
}

void TestNavigation::crumbsStartAtHomeWhenUnderHome()
{
    Platform platform;
    const QString home = QDir::homePath();

    const QVariantList crumbs = platform.pathCrumbs(home + QStringLiteral("/Projects/omanta"));
    QCOMPARE(crumbs.size(), 3);
    QCOMPARE(crumbs.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Home"));
    QCOMPARE(crumbs.at(0).toMap().value(QStringLiteral("path")).toString(), home);
    QCOMPARE(crumbs.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Projects"));
    QCOMPARE(crumbs.at(2).toMap().value(QStringLiteral("path")).toString(),
             home + QStringLiteral("/Projects/omanta"));

    // Home itself is a single crumb.
    QCOMPARE(platform.pathCrumbs(home).size(), 1);
}

void TestNavigation::crumbsStartAtRootOtherwise()
{
    Platform platform;

    const QVariantList crumbs = platform.pathCrumbs(QStringLiteral("/usr/share/icons"));
    QCOMPARE(crumbs.size(), 4);
    QCOMPARE(crumbs.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("/"));
    QCOMPARE(crumbs.at(1).toMap().value(QStringLiteral("path")).toString(), QStringLiteral("/usr"));
    QCOMPARE(crumbs.at(3).toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/usr/share/icons"));

    QVERIFY(platform.pathCrumbs(QString()).isEmpty());
}

void TestNavigation::formatsSizes()
{
    Platform platform;
    // Delegated to GLib so omanta and every GTK app agree; just check it
    // produces something sane rather than asserting exact wording.
    QVERIFY(!platform.formatSize(0).isEmpty());
    QVERIFY(platform.formatSize(1500).contains(QLatin1String("kB"))
            || platform.formatSize(1500).contains(QLatin1String("KB")));
}

void TestNavigation::uriDetectionAndLocality()
{
    Platform platform;
    QVERIFY(platform.isLocal(QStringLiteral("/tmp")));
    QVERIFY(platform.isLocal(QStringLiteral("file:///tmp")));
    QVERIFY(!platform.isLocal(QStringLiteral("trash:///")));
    QVERIFY(!platform.isLocal(QStringLiteral("smb://server/share")));

    // Navigation trusts URIs (a sync stat would block on the network) but
    // still stat-checks local paths.
    QVERIFY(platform.isNavigable(QStringLiteral("trash:///")));
    QVERIFY(platform.isNavigable(QStringLiteral("smb://server/share")));
    QVERIFY(platform.isNavigable(QDir::homePath()));
    QVERIFY(!platform.isNavigable(QDir::homePath() + QStringLiteral("/definitely-not-here-xyz")));
}

void TestNavigation::uriParentStopsAtItsRoot()
{
    Platform platform;
    // The top of a virtual filesystem is a root exactly like "/": Up ends.
    QCOMPARE(platform.parentPath(QStringLiteral("trash:///")), QString());
    QVERIFY(platform.parentPath(QStringLiteral("smb://server/share"))
                .startsWith(QStringLiteral("smb://server")));
}

void TestNavigation::uriDisplayNames()
{
    Platform platform;
    QCOMPARE(platform.baseName(QStringLiteral("trash:///")), QStringLiteral("Trash"));
    QCOMPARE(platform.baseName(QStringLiteral("recent:///")), QStringLiteral("Recent"));
    QCOMPARE(platform.baseName(QStringLiteral("network:///")), QStringLiteral("Network"));
    QCOMPARE(platform.baseName(QStringLiteral("smb://server/")), QStringLiteral("server"));
    QCOMPARE(platform.baseName(QStringLiteral("smb://server/share/docs")), QStringLiteral("docs"));
    // Locals unchanged by the conversion.
    QCOMPARE(platform.baseName(QStringLiteral("/")), QStringLiteral("/"));
    QCOMPARE(platform.baseName(QStringLiteral("/tmp/notes.txt")), QStringLiteral("notes.txt"));
}

void TestNavigation::uriCrumbs()
{
    Platform platform;
    const QVariantList crumbs =
        platform.pathCrumbs(QStringLiteral("smb://server/share/docs"));

    QCOMPARE(crumbs.size(), 3);
    QCOMPARE(crumbs.at(0).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("server"));
    QCOMPARE(crumbs.at(1).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("share"));
    QCOMPARE(crumbs.at(2).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("docs"));
    // Every crumb is itself navigable input.
    QVERIFY(crumbs.at(1).toMap().value(QStringLiteral("path")).toString()
                .startsWith(QStringLiteral("smb://server/share")));

    const QVariantList trash = platform.pathCrumbs(QStringLiteral("trash:///"));
    QCOMPARE(trash.size(), 1);
    QCOMPARE(trash.at(0).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Trash"));
}

void TestNavigation::typedUrisPassThroughResolve()
{
    Platform platform;
    // Ctrl+L input: a URI names the place itself, whatever folder is current.
    QCOMPARE(platform.resolvePath(QStringLiteral("trash:///"), QStringLiteral("/tmp")),
             QStringLiteral("trash:///"));
    QVERIFY(platform.resolvePath(QStringLiteral("smb://server/share"), QStringLiteral("/tmp"))
                .startsWith(QStringLiteral("smb://server/share")));
}

void TestNavigation::fileUrisNormalizeToPaths()
{
    Platform platform;
    // file:// is local in disguise — after resolving, it must be a plain path
    // so history and "already showing this folder" comparisons keep working.
    QCOMPARE(platform.resolvePath(QStringLiteral("file:///tmp"), QString()),
             QStringLiteral("/tmp"));
}

void TestNavigation::rendersAndParsesUriLists()
{
    Platform platform;

    // Out: paths become file:// URIs, URIs stay themselves, CRLF-separated —
    // the exact payload a drag hands to another application.
    const QString list = platform.uriList({ QStringLiteral("/tmp/a file.txt"),
                                            QStringLiteral("trash:///") });
    const QStringList lines = list.split(QStringLiteral("\r\n"));
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QStringLiteral("file:///tmp/a%20file.txt"));
    QCOMPARE(lines.at(1), QStringLiteral("trash:///"));

    // In: a drop's URLs come back as locations — file:// to plain paths,
    // escaping undone, foreign URIs preserved.
    const QStringList locations = platform.locationsFromUrls(
        { QUrl(QStringLiteral("file:///tmp/a%20file.txt")),
          QUrl(QStringLiteral("smb://server/share/doc.txt")) });
    QCOMPARE(locations.size(), 2);
    QCOMPARE(locations.at(0), QStringLiteral("/tmp/a file.txt"));
    QVERIFY(locations.at(1).startsWith(QStringLiteral("smb://server/share")));
}

void TestNavigation::sameFilesystemAnswersForLocalPaths()
{
    Platform platform;
    // Two directories under $HOME share a filesystem; an unanswerable side
    // (nonexistent path) must come back false — copy is the safe default.
    QVERIFY(platform.sameFilesystem(QDir::homePath(), QDir::homePath()));
    QVERIFY(!platform.sameFilesystem(QDir::homePath(),
                                     QStringLiteral("/no/such/place/at/all")));
    QVERIFY(!platform.sameFilesystem(QStringLiteral("smb://unreachable/x"),
                                     QDir::homePath()));
}

void TestNavigation::templatesListsVisibleFilesSorted()
{
    QTemporaryDir dir;
    auto write = [&](const QString &name) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    };
    write(QStringLiteral("Text Document.txt"));
    write(QStringLiteral("Spreadsheet.ods"));
    write(QStringLiteral(".hidden.txt"));
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("a-subdir"))); // dirs are not templates

    qputenv("OMANTA_TEMPLATES_DIR", dir.path().toUtf8());
    Platform platform;
    const QVariantList list = platform.templates();
    qunsetenv("OMANTA_TEMPLATES_DIR");

    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value("name").toString(), QStringLiteral("Spreadsheet.ods"));
    QCOMPARE(list.at(1).toMap().value("name").toString(), QStringLiteral("Text Document.txt"));
    QCOMPARE(list.at(1).toMap().value("path").toString(),
             dir.filePath(QStringLiteral("Text Document.txt")));
}

void TestNavigation::templatesMissingDirIsEmpty()
{
    qputenv("OMANTA_TEMPLATES_DIR", "/no/such/templates/dir");
    Platform platform;
    QCOMPARE(platform.templates().size(), 0);
    qunsetenv("OMANTA_TEMPLATES_DIR");
}

QTEST_GUILESS_MAIN(TestNavigation)
#include "tst_navigation.moc"

void TestNavigation::activationExtractsOnlyWhenSelfIsDefault()
{
    // A private XDG world: omanta.desktop is the default for zip and
    // nothing else. Must run after every other GAppInfo-free test — GIO
    // snapshots these variables on first app-info use.
    QTemporaryDir world;
    QVERIFY(world.isValid());
    QDir().mkpath(world.filePath("config"));
    QDir().mkpath(world.filePath("share/applications"));

    QFile apps(world.filePath("config/mimeapps.list"));
    QVERIFY(apps.open(QIODevice::WriteOnly));
    apps.write("[Default Applications]\napplication/zip=omanta.desktop\n");
    apps.close();

    // GIO drops a desktop file whose Exec binary is not in PATH, so give
    // the private world its own stub — the test must not depend on omanta
    // being installed on the build machine.
    QDir().mkpath(world.filePath("bin"));
    QFile stub(world.filePath("bin/omanta"));
    QVERIFY(stub.open(QIODevice::WriteOnly));
    stub.write("#!/bin/sh\nexit 0\n");
    stub.close();
    QVERIFY(stub.setPermissions(stub.permissions() | QFileDevice::ExeOwner));
    qputenv("PATH", (world.filePath("bin") + QStringLiteral(":")
                     + qEnvironmentVariable("PATH")).toUtf8());

    QFile desktop(world.filePath("share/applications/omanta.desktop"));
    QVERIFY(desktop.open(QIODevice::WriteOnly));
    desktop.write("[Desktop Entry]\nType=Application\nName=Files\n"
                  "Exec=omanta %U\nMimeType=application/zip;\n");
    desktop.close();

    qputenv("XDG_CONFIG_HOME", world.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", world.filePath("share").toUtf8());
    qputenv("XDG_DATA_DIRS", world.filePath("share").toUtf8());

    Platform platform;
    // Nautilus 50's rule, both halves: extract when we are the default...
    QVERIFY(platform.activationExtracts(QStringLiteral("application/zip")));
    // ...open with the app otherwise — unclaimed archive type, and a
    // non-archive type even if someone made us its default.
    QVERIFY(!platform.activationExtracts(QStringLiteral("application/x-tar")));
    QVERIFY(!platform.activationExtracts(QStringLiteral("text/plain")));
}
