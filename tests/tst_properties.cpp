#include "FileProperties.h"
#include "TestFixture.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <sys/stat.h>

// What the properties dialog claims about a file, checked against the file.
//
// Everything here is asynchronous — a folder total is a tree walk — so each
// test waits for the read to settle rather than assuming it has.
class TestProperties : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // GIO's getters are not queries: several of them assert the attribute is
    // present and log a CRITICAL when it is not, then return null. Reading one
    // wrongly therefore produces a working-looking app that spews to stderr —
    // which is invisible unless something makes it fatal. Something does now.
    void initTestCase() { g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL); }

    void readsASingleFile();
    void reportsTheParentAsLocation();
    void followsASymlinkButSaysItIsOne();

    void totalsAFolderRecursively();
    void countsContentsWithoutCountingTheFolderItself();
    void totalsAMultipleSelection();
    void ignoresAnUnreadableSubdirectory();

    void reportsPermissionsAsTextAndOctal();
    void rendersTheSpecialBits();
    void applyModeChangesThePermissionsOnDisk();
    void refusesToApplyToAMultipleSelection();

    void listsApplicationsForTheType();
    void offersNoApplicationsForAFolderOrAMultipleSelection();

    void reportsTheFilesystem();
    void reportsAMissingFileAsAnError();
    void clearsEverythingWhenThePathsAreCleared();
    void survivesBeingDestroyedMidMeasurement();

private:
    // The read is done when it says it is loaded and nothing is still walking.
    static bool settle(FileProperties &properties, int timeoutMs = 20000)
    {
        QElapsedTimer timer;
        timer.start();
        while ((!properties.loaded() || properties.measuring()) && timer.elapsed() < timeoutMs)
            QTest::qWait(10);
        return properties.loaded() && !properties.measuring();
    }
};

void TestProperties::readsASingleFile()
{
    TempTree tree;
    tree.writeFile(QStringLiteral("notes.txt"), 1234);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("notes.txt")) });
    QVERIFY(settle(properties));

    QCOMPARE(properties.displayName(), QStringLiteral("notes.txt"));
    QCOMPARE(properties.itemCount(), 1);
    QCOMPARE(properties.size(), 1234);
    QCOMPARE(properties.fileCount(), 1);
    QCOMPARE(properties.folderCount(), 0);
    QVERIFY(!properties.isDir());
    QVERIFY(!properties.isSymlink());
    QCOMPARE(properties.contentType(), QStringLiteral("text/plain"));
    QVERIFY(!properties.typeDescription().isEmpty());
    QVERIFY(properties.modified().isValid());
    QVERIFY(!properties.owner().isEmpty());
    QVERIFY(properties.errorMessage().isEmpty());

    // Allocated size is what the filesystem actually spent, so it is a multiple
    // of the block size rather than the byte count.
    QVERIFY(properties.sizeOnDisk() >= properties.size());
}

void TestProperties::reportsTheParentAsLocation()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("photos"));
    tree.writeFile(QStringLiteral("photos/roll.jpg"), 10);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("photos/roll.jpg")) });
    QVERIFY(settle(properties));

    QCOMPARE(properties.location(), tree.filePath(QStringLiteral("photos")));
}

void TestProperties::followsASymlinkButSaysItIsOne()
{
    TempTree tree;
    tree.writeFile(QStringLiteral("real.txt"), 500);
    QVERIFY(QFile::link(tree.filePath(QStringLiteral("real.txt")),
                        tree.filePath(QStringLiteral("link.txt"))));

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("link.txt")) });
    QVERIFY(settle(properties));

    QVERIFY(properties.isSymlink());
    QVERIFY(properties.symlinkTarget().endsWith(QStringLiteral("real.txt")));
    // The size a user cares about is the target's, not the link's.
    QCOMPARE(properties.size(), 500);
    QCOMPARE(properties.contentType(), QStringLiteral("text/plain"));
}

void TestProperties::totalsAFolderRecursively()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("project"));
    tree.writeFile(QStringLiteral("project/a.txt"), 1000);
    tree.writeFile(QStringLiteral("project/deep/b.txt"), 2000);
    tree.writeFile(QStringLiteral("project/deep/deeper/c.txt"), 3000);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("project")) });
    QVERIFY(settle(properties));

    // The directory entries themselves take space, so the total is the file
    // bytes plus whatever the filesystem charges for the three directories.
    QVERIFY(properties.size() >= 6000);
    QVERIFY(properties.isDir());
    QVERIFY(properties.errorMessage().isEmpty());
}

void TestProperties::countsContentsWithoutCountingTheFolderItself()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("box"));
    tree.writeFile(QStringLiteral("box/one"), 1);
    tree.writeFile(QStringLiteral("box/two"), 1);
    tree.makeDir(QStringLiteral("box/inner"));
    tree.writeFile(QStringLiteral("box/inner/three"), 1);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("box")) });
    QVERIFY(settle(properties));

    QCOMPARE(properties.fileCount(), 3);
    // "box" holds one folder. Counting itself would make every empty folder
    // claim to contain one.
    QCOMPARE(properties.folderCount(), 1);
}

void TestProperties::totalsAMultipleSelection()
{
    TempTree tree;
    tree.writeFile(QStringLiteral("loose.bin"), 100);
    tree.makeDir(QStringLiteral("folder"));
    tree.writeFile(QStringLiteral("folder/inside.bin"), 250);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("loose.bin")),
                          tree.filePath(QStringLiteral("folder")) });
    QVERIFY(settle(properties));

    QCOMPARE(properties.itemCount(), 2);
    QVERIFY(properties.size() >= 350);
    QCOMPARE(properties.fileCount(), 2);
    QCOMPARE(properties.folderCount(), 0); // "folder" is selected, not contained
}

void TestProperties::ignoresAnUnreadableSubdirectory()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("mixed"));
    tree.writeFile(QStringLiteral("mixed/readable.txt"), 400);
    const QString locked = tree.makeDir(QStringLiteral("mixed/locked"));
    tree.writeFile(QStringLiteral("mixed/locked/hidden.txt"), 999);
    QVERIFY(::chmod(locked.toUtf8().constData(), 0000) == 0);

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("mixed")) });
    const bool settled = settle(properties);

    // Restore before asserting, or the temporary tree cannot be removed.
    ::chmod(locked.toUtf8().constData(), 0755);

    QVERIFY(settled);
    // A folder you cannot open contributes nothing rather than aborting the
    // whole total — a properties dialog that gives up on one unreadable
    // subdirectory is useless on any real system directory.
    QVERIFY(properties.size() >= 400);
    QCOMPARE(properties.fileCount(), 1);
}

void TestProperties::reportsPermissionsAsTextAndOctal()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("script.sh"), 20);
    QVERIFY(::chmod(path.toUtf8().constData(), 0754) == 0);

    FileProperties properties;
    properties.setPaths({ path });
    QVERIFY(settle(properties));

    QCOMPARE(properties.mode(), 0754);
    QCOMPARE(properties.modeText(), QStringLiteral("rwxr-xr--"));
    QCOMPARE(properties.modeOctal(), QStringLiteral("0754"));
    QVERIFY(properties.canChangeMode()); // we made the file, so it is ours
}

void TestProperties::rendersTheSpecialBits()
{
    // Pure formatting, so it is checked directly rather than by chmodding real
    // files — setuid on a temporary file is not something a test should leave
    // lying around.
    QCOMPARE(FileProperties::modeToText(0644), QStringLiteral("rw-r--r--"));
    QCOMPARE(FileProperties::modeToText(0755), QStringLiteral("rwxr-xr-x"));
    QCOMPARE(FileProperties::modeToText(0000), QStringLiteral("---------"));
    QCOMPARE(FileProperties::modeToText(0777), QStringLiteral("rwxrwxrwx"));

    QCOMPARE(FileProperties::modeToText(04755), QStringLiteral("rwsr-xr-x"));
    QCOMPARE(FileProperties::modeToText(04644), QStringLiteral("rwSr--r--"));
    QCOMPARE(FileProperties::modeToText(02755), QStringLiteral("rwxr-sr-x"));
    QCOMPARE(FileProperties::modeToText(01777), QStringLiteral("rwxrwxrwt"));
    QCOMPARE(FileProperties::modeToText(01666), QStringLiteral("rw-rw-rwT"));

    QCOMPARE(FileProperties::modeToText(-1), QString());
}

void TestProperties::applyModeChangesThePermissionsOnDisk()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("data.txt"), 20);
    QVERIFY(::chmod(path.toUtf8().constData(), 0644) == 0);

    FileProperties properties;
    properties.setPaths({ path });
    QVERIFY(settle(properties));
    QCOMPARE(properties.mode(), 0644);

    properties.applyMode(0600);
    QTest::qWait(50);
    QVERIFY(settle(properties));

    struct stat status = {};
    QCOMPARE(::stat(path.toUtf8().constData(), &status), 0);
    QCOMPARE(int(status.st_mode & 07777), 0600);

    // And the object re-read rather than trusting its own write.
    QCOMPARE(properties.mode(), 0600);
}

void TestProperties::refusesToApplyToAMultipleSelection()
{
    TempTree tree;
    const QString first = tree.writeFile(QStringLiteral("one.txt"), 5);
    const QString second = tree.writeFile(QStringLiteral("two.txt"), 5);
    QVERIFY(::chmod(first.toUtf8().constData(), 0644) == 0);
    QVERIFY(::chmod(second.toUtf8().constData(), 0644) == 0);

    FileProperties properties;
    properties.setPaths({ first, second });
    QVERIFY(settle(properties));

    QVERIFY(!properties.canChangeMode());

    properties.applyMode(0600);
    QTest::qWait(100);

    // Two files with one set of checkboxes is a way to change the wrong one.
    struct stat status = {};
    QCOMPARE(::stat(first.toUtf8().constData(), &status), 0);
    QCOMPARE(int(status.st_mode & 07777), 0644);
}

void TestProperties::listsApplicationsForTheType()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("readme.txt"), 10);

    FileProperties properties;
    properties.setPaths({ path });
    QVERIFY(settle(properties));
    QCOMPARE(properties.contentType(), QStringLiteral("text/plain"));

    const QVariantList applications = properties.applications();
    if (applications.isEmpty())
        QSKIP("no application on this system is registered for text/plain");

    // Every entry has to carry the three things the list actually renders, and
    // an id that setDefaultApplication can look back up.
    int defaults = 0;
    for (const QVariant &entry : applications) {
        const QVariantMap application = entry.toMap();
        QVERIFY(!application.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!application.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(application.value(QStringLiteral("iconSource")).toString()
                    .startsWith(QStringLiteral("image://fileicon/")));
        if (application.value(QStringLiteral("isDefault")).toBool())
            ++defaults;
    }

    // At most one of them can be the default — two would mean the list is
    // marking whatever it feels like rather than asking GIO.
    QVERIFY(defaults <= 1);
}

void TestProperties::offersNoApplicationsForAFolderOrAMultipleSelection()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("folder"));
    const QString first = tree.writeFile(QStringLiteral("a.txt"), 4);
    const QString second = tree.writeFile(QStringLiteral("b.txt"), 4);

    FileProperties folder;
    folder.setPaths({ tree.filePath(QStringLiteral("folder")) });
    QVERIFY(settle(folder));
    // "Open With" on a folder would offer to hand it to a text editor.
    QVERIFY(folder.applications().isEmpty());

    FileProperties several;
    several.setPaths({ first, second });
    QVERIFY(settle(several));
    QVERIFY(several.applications().isEmpty());
}

void TestProperties::reportsTheFilesystem()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("any.txt"), 10);

    FileProperties properties;
    properties.setPaths({ path });
    QVERIFY(settle(properties));

    QVERIFY(!properties.filesystemType().isEmpty());
    QVERIFY(properties.filesystemSize() > 0);
}

void TestProperties::reportsAMissingFileAsAnError()
{
    TempTree tree;

    FileProperties properties;
    properties.setPaths({ tree.filePath(QStringLiteral("was-never-here.txt")) });
    QVERIFY(settle(properties));

    QVERIFY(!properties.errorMessage().isEmpty());
    QCOMPARE(properties.size(), 0);
}

void TestProperties::clearsEverythingWhenThePathsAreCleared()
{
    TempTree tree;

    FileProperties properties;
    properties.setPaths({ tree.writeFile(QStringLiteral("gone.txt"), 42) });
    QVERIFY(settle(properties));
    QCOMPARE(properties.size(), 42);

    properties.setPaths({});

    // A dialog reopened on nothing must not still be showing the last file.
    QCOMPARE(properties.itemCount(), 0);
    QCOMPARE(properties.size(), 0);
    QVERIFY(properties.displayName().isEmpty());
    QVERIFY(!properties.loaded());
}

void TestProperties::survivesBeingDestroyedMidMeasurement()
{
    TempTree tree;
    tree.makeDir(QStringLiteral("wide"));
    for (int i = 0; i < 400; ++i)
        tree.writeFile(QStringLiteral("wide/file-%1").arg(i), 64);

    {
        FileProperties properties;
        properties.setPaths({ tree.filePath(QStringLiteral("wide")) });
        // Deliberately does not wait: closing the dialog while a big folder is
        // still being walked is the ordinary case, not the exotic one.
    }

    // The walk's callbacks land after the object is gone. Nothing should touch
    // it — this test is here to fail under ASan if that ever stops being true.
    QTest::qWait(300);
}

QTEST_GUILESS_MAIN(TestProperties)
#include "tst_properties.moc"
