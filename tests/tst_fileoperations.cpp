#include "ArchiveEngine.h"
#include "FileOperations.h"
#include "TestFixture.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

// Every destructive path in the app, exercised against throwaway trees before
// it is ever pointed at real data.
//
// The assertions are deliberately about the *filesystem*, not about signals: a
// green signal with the wrong bytes on disk is exactly the failure this suite
// exists to catch.
class TestFileOperations : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void createsAFolder();
    void undoOfCreateRemovesIt();

    void renamesAFile();
    void undoOfRenameRestoresTheName();
    void refusesAnEmptyOrSlashedName();

    void trashesFiles();
    void undoOfTrashRestoresThem();
    void restoresFromTrashByOriginalPath();
    void undoOfRestoreTrashesAgain();

    void copiesAFile();
    void copiesADirectoryTree();
    void copyRenamesRatherThanClobbering();
    void copyCanReplaceWhenAsked();
    void copyCanSkipWhenAsked();
    void undoOfCopyRemovesOnlyTheCopies();
    void mergesDirectoriesRespectingPolicy();

    void movesAFile();
    void movesADirectoryTree();
    void undoOfMovePutsItBack();

    void createsLinks();
    void linkNamesAvoidClashes();
    void undoOfCreateLinkRemovesOnlyTheLinks();

    void refusesToCopyAFolderIntoItself();
    void deletePermanentlyLeavesNoUndo();
    void deleteRemovesATreeEntirely();
    void deleteDoesNotFollowSymlinksOutOfTheTree();
    void copyPreservesSymlinksRatherThanDereferencingThem();
    void copySurvivesASymlinkLoop();

    void reportsErrorsWithoutRecordingUndo();

    void operationsListRunningThenQueued();
    void cancelDropsAQueuedOperation();
    void remainingTextEstimates();
    void transferredTextFormats();
    void encryptedExtractAsksThenReplays();
    void decliningThePassphraseDropsTheRequest();

    void redoReplaysAnUndoneCopy();
    void redoReproducesConflictRenames();
    void redoOfRenameWalksBothWays();
    void freshOperationClearsTheRedoStack();
    void redoChainsThroughSeveralUndos();

private:
    // Operations are asynchronous by design; every test waits on the queue
    // draining rather than sleeping a fixed amount.
    static bool settle(FileOperations &ops, int timeoutMs = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        // Give the queue a moment to pick the job up before waiting on it.
        while (!ops.busy() && timer.elapsed() < 500)
            QTest::qWait(10);
        while (ops.busy() && timer.elapsed() < timeoutMs)
            QTest::qWait(20);
        return !ops.busy();
    }

    static QString read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
};

void TestFileOperations::createsAFolder()
{
    TempTree tree;
    FileOperations ops;

    ops.createFolder(tree.path(), QStringLiteral("New Folder"));
    QVERIFY(settle(ops));

    QVERIFY(QFileInfo(tree.filePath("New Folder")).isDir());
    QVERIFY(ops.canUndo());
    QVERIFY(ops.lastError().isEmpty());
}

void TestFileOperations::undoOfCreateRemovesIt()
{
    TempTree tree;
    FileOperations ops;

    ops.createFolder(tree.path(), QStringLiteral("Temporary"));
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("Temporary")));

    ops.undo();
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("Temporary")));
    // Undoing must not itself be undoable, or Ctrl+Z ping-pongs.
    QVERIFY(!ops.canUndo());
}

void TestFileOperations::renamesAFile()
{
    TempTree tree;
    tree.writeFile("before.txt");
    FileOperations ops;

    ops.rename(tree.filePath("before.txt"), QStringLiteral("after.txt"));
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("before.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("after.txt")));
}

void TestFileOperations::undoOfRenameRestoresTheName()
{
    TempTree tree;
    tree.writeFile("original.txt");
    FileOperations ops;

    ops.rename(tree.filePath("original.txt"), QStringLiteral("changed.txt"));
    QVERIFY(settle(ops));
    ops.undo();
    QVERIFY(settle(ops));

    QVERIFY(QFileInfo::exists(tree.filePath("original.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("changed.txt")));
}

void TestFileOperations::refusesAnEmptyOrSlashedName()
{
    TempTree tree;
    tree.writeFile("file.txt");
    FileOperations ops;

    ops.rename(tree.filePath("file.txt"), QString());
    QVERIFY(!ops.lastError().isEmpty());
    ops.clearError();

    // A name containing a separator is a path, not a rename — allowing it would
    // silently move the file somewhere the user never chose.
    ops.rename(tree.filePath("file.txt"), QStringLiteral("../escaped.txt"));
    QVERIFY(!ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("file.txt")));
    QVERIFY(!QFileInfo::exists(QDir(tree.path()).filePath("../escaped.txt")));
}

void TestFileOperations::trashesFiles()
{
    TempTree tree(TempTree::UnderHome);
    tree.writeFile("rubbish.txt");
    FileOperations ops;

    ops.trash({ tree.filePath("rubbish.txt") });
    QVERIFY(settle(ops));

    if (ops.lastError().contains(QLatin1String("not supported")))
        QSKIP("this filesystem has no trash directory");

    QVERIFY2(ops.lastError().isEmpty(), qPrintable(ops.lastError()));
    QVERIFY(!QFileInfo::exists(tree.filePath("rubbish.txt")));
}

void TestFileOperations::undoOfTrashRestoresThem()
{
    TempTree tree(TempTree::UnderHome);
    const QString path = tree.filePath("recoverable.txt");
    tree.writeFile("recoverable.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("precious");
    file.close();

    FileOperations ops;
    ops.trash({ path });
    QVERIFY(settle(ops));

    if (ops.lastError().contains(QLatin1String("not supported")))
        QSKIP("this filesystem has no trash directory");
    QVERIFY(!QFileInfo::exists(path));

    ops.undo();
    QVERIFY(settle(ops));

    QVERIFY2(ops.lastError().isEmpty(), qPrintable(ops.lastError()));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(read(path), QStringLiteral("precious"));
}

void TestFileOperations::restoresFromTrashByOriginalPath()
{
    TempTree tree(TempTree::UnderHome);
    const QString path = tree.filePath("wanted-back.txt");
    tree.writeFile("wanted-back.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("still precious");
    file.close();

    FileOperations ops;
    ops.trash({ path });
    QVERIFY(settle(ops));
    if (ops.lastError().contains(QLatin1String("not supported")))
        QSKIP("this filesystem has no trash directory");
    QVERIFY(!QFileInfo::exists(path));

    // The public restore — what "Restore from Trash" in the trash view calls —
    // takes the original path, exactly as the origPath role hands it over.
    ops.restoreFromTrash({ path });
    QVERIFY(settle(ops));

    QVERIFY2(ops.lastError().isEmpty(), qPrintable(ops.lastError()));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(read(path), QStringLiteral("still precious"));
}

void TestFileOperations::undoOfRestoreTrashesAgain()
{
    TempTree tree(TempTree::UnderHome);
    const QString path = tree.filePath("boomerang.txt");
    tree.writeFile("boomerang.txt");

    FileOperations ops;
    ops.trash({ path });
    QVERIFY(settle(ops));
    if (ops.lastError().contains(QLatin1String("not supported")))
        QSKIP("this filesystem has no trash directory");

    // Trash, then undo (restore): the undo itself must record nothing, or
    // Ctrl+Z would ping-pong instead of walking history.
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!ops.canUndo());

    // A deliberate restore, though, is a real operation and undoes cleanly.
    ops.trash({ path });
    QVERIFY(settle(ops));
    ops.restoreFromTrash({ path });
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(path));

    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY2(ops.lastError().isEmpty(), qPrintable(ops.lastError()));
    QVERIFY(!QFileInfo::exists(path));

    // Leave nothing of ours in the real trash.
    ops.restoreFromTrash({ path });
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(path));
}

void TestFileOperations::copiesAFile()
{
    TempTree tree;
    tree.writeFile("source.txt", 64);
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("source.txt") }, tree.filePath("target"));
    QVERIFY(settle(ops));

    QVERIFY(QFileInfo::exists(tree.filePath("source.txt")));         // original intact
    QVERIFY(QFileInfo::exists(tree.filePath("target/source.txt")));
    QCOMPARE(QFileInfo(tree.filePath("target/source.txt")).size(), 64);
}

void TestFileOperations::copiesADirectoryTree()
{
    TempTree tree;
    tree.writeFile("tree/a.txt", 10);
    tree.writeFile("tree/nested/b.txt", 20);
    tree.writeFile("tree/nested/deeper/c.txt", 30);
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("tree") }, tree.filePath("target"));
    QVERIFY(settle(ops));

    QVERIFY2(ops.lastError().isEmpty(), qPrintable(ops.lastError()));
    QVERIFY(QFileInfo(tree.filePath("target/tree")).isDir());
    QVERIFY(QFileInfo(tree.filePath("target/tree/nested/deeper")).isDir());
    QCOMPARE(QFileInfo(tree.filePath("target/tree/a.txt")).size(), 10);
    QCOMPARE(QFileInfo(tree.filePath("target/tree/nested/b.txt")).size(), 20);
    QCOMPARE(QFileInfo(tree.filePath("target/tree/nested/deeper/c.txt")).size(), 30);

    // The original must be untouched.
    QVERIFY(QFileInfo::exists(tree.filePath("tree/nested/deeper/c.txt")));
}

void TestFileOperations::copyRenamesRatherThanClobbering()
{
    TempTree tree;
    tree.makeDir("target");
    tree.writeFile("notes.txt", 5);
    tree.writeFile("target/notes.txt", 999);
    FileOperations ops;

    ops.copy({ tree.filePath("notes.txt") }, tree.filePath("target"));
    QVERIFY(settle(ops));

    // The default policy must never destroy an existing file.
    QCOMPARE(QFileInfo(tree.filePath("target/notes.txt")).size(), 999);
    QVERIFY(QFileInfo::exists(tree.filePath("target/notes (copy).txt")));
    QCOMPARE(QFileInfo(tree.filePath("target/notes (copy).txt")).size(), 5);
}

void TestFileOperations::copyCanReplaceWhenAsked()
{
    TempTree tree;
    tree.makeDir("target");
    tree.writeFile("data.txt", 5);
    tree.writeFile("target/data.txt", 999);
    FileOperations ops;

    ops.copy({ tree.filePath("data.txt") }, tree.filePath("target"), FileOperations::Replace);
    QVERIFY(settle(ops));

    QCOMPARE(QFileInfo(tree.filePath("target/data.txt")).size(), 5);
    QVERIFY(!QFileInfo::exists(tree.filePath("target/data (copy).txt")));
}

void TestFileOperations::copyCanSkipWhenAsked()
{
    TempTree tree;
    tree.makeDir("target");
    tree.writeFile("data.txt", 5);
    tree.writeFile("target/data.txt", 999);
    FileOperations ops;

    ops.copy({ tree.filePath("data.txt") }, tree.filePath("target"), FileOperations::Skip);
    QVERIFY(settle(ops));

    QCOMPARE(QFileInfo(tree.filePath("target/data.txt")).size(), 999);
    QVERIFY(!QFileInfo::exists(tree.filePath("target/data (copy).txt")));
}

void TestFileOperations::undoOfCopyRemovesOnlyTheCopies()
{
    TempTree tree;
    tree.writeFile("keep.txt", 7);
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("keep.txt") }, tree.filePath("target"));
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("target/keep.txt")));

    ops.undo();
    QVERIFY(settle(ops));

    // The copy goes; the original must survive. Getting this backwards would
    // delete the user's file to undo a copy.
    QVERIFY(!QFileInfo::exists(tree.filePath("target/keep.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("keep.txt")));
    QCOMPARE(QFileInfo(tree.filePath("keep.txt")).size(), 7);
}

void TestFileOperations::mergesDirectoriesRespectingPolicy()
{
    // Copying a folder onto a folder of the same name merges rather than
    // failing — and the nested collisions inside it must obey the same policy
    // as a top-level one. This is where a stray overwrite would quietly eat a
    // file several levels down, long after the user stopped watching.
    auto build = [](const TempTree &tree) {
        tree.writeFile("src/shared.txt", 11);
        tree.writeFile("src/only-in-source.txt", 22);
        tree.makeDir("dest");
        tree.writeFile("dest/src/shared.txt", 999);
        tree.writeFile("dest/src/only-in-dest.txt", 33);
    };

    {
        TempTree tree;
        build(tree);
        FileOperations ops;
        ops.copy({ tree.filePath("src") }, tree.filePath("dest"), FileOperations::RenameNew);
        QVERIFY(settle(ops));

        QCOMPARE(QFileInfo(tree.filePath("dest/src/shared.txt")).size(), 999);       // untouched
        QCOMPARE(QFileInfo(tree.filePath("dest/src/shared (copy).txt")).size(), 11); // renamed
        QVERIFY(QFileInfo::exists(tree.filePath("dest/src/only-in-dest.txt")));      // preserved
        QCOMPARE(QFileInfo(tree.filePath("dest/src/only-in-source.txt")).size(), 22); // merged in
    }

    {
        TempTree tree;
        build(tree);
        FileOperations ops;
        ops.copy({ tree.filePath("src") }, tree.filePath("dest"), FileOperations::Replace);
        QVERIFY(settle(ops));

        QCOMPARE(QFileInfo(tree.filePath("dest/src/shared.txt")).size(), 11);   // overwritten
        QVERIFY(QFileInfo::exists(tree.filePath("dest/src/only-in-dest.txt")));  // still preserved
    }

    {
        TempTree tree;
        build(tree);
        FileOperations ops;
        ops.copy({ tree.filePath("src") }, tree.filePath("dest"), FileOperations::Skip);
        QVERIFY(settle(ops));

        QCOMPARE(QFileInfo(tree.filePath("dest/src/shared.txt")).size(), 999);  // left alone
        QVERIFY(!QFileInfo::exists(tree.filePath("dest/src/shared (copy).txt")));
        QCOMPARE(QFileInfo(tree.filePath("dest/src/only-in-source.txt")).size(), 22); // still merged
    }
}

void TestFileOperations::movesAFile()
{
    TempTree tree;
    tree.writeFile("wanderer.txt", 12);
    tree.makeDir("elsewhere");
    FileOperations ops;

    ops.move({ tree.filePath("wanderer.txt") }, tree.filePath("elsewhere"));
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("wanderer.txt")));
    QCOMPARE(QFileInfo(tree.filePath("elsewhere/wanderer.txt")).size(), 12);
}

void TestFileOperations::movesADirectoryTree()
{
    TempTree tree;
    tree.writeFile("bundle/inner/file.txt", 33);
    tree.makeDir("destination");
    FileOperations ops;

    ops.move({ tree.filePath("bundle") }, tree.filePath("destination"));
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("bundle")));
    QCOMPARE(QFileInfo(tree.filePath("destination/bundle/inner/file.txt")).size(), 33);
}

void TestFileOperations::undoOfMovePutsItBack()
{
    TempTree tree;
    tree.writeFile("returning.txt", 9);
    tree.makeDir("away");
    FileOperations ops;

    ops.move({ tree.filePath("returning.txt") }, tree.filePath("away"));
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("away/returning.txt")));

    ops.undo();
    QVERIFY(settle(ops));

    QCOMPARE(QFileInfo(tree.filePath("returning.txt")).size(), 9);
    QVERIFY(!QFileInfo::exists(tree.filePath("away/returning.txt")));
}

void TestFileOperations::createsLinks()
{
    TempTree tree;
    FileOperations ops;
    tree.writeFile(QStringLiteral("notes.txt"));
    tree.makeDir(QStringLiteral("dest"));

    ops.createLink({ tree.filePath("notes.txt") }, tree.filePath("dest"));
    QVERIFY(settle(ops));

    const QFileInfo link(tree.filePath("dest/Link to notes.txt"));
    QVERIFY(link.isSymLink());
    // The symlink value is the absolute source path, as Nautilus writes it.
    QCOMPARE(link.symLinkTarget(), tree.filePath("notes.txt"));
    QVERIFY(ops.canUndo());
    QVERIFY(ops.lastError().isEmpty());
}

void TestFileOperations::linkNamesAvoidClashes()
{
    TempTree tree;
    FileOperations ops;
    tree.writeFile(QStringLiteral("notes.txt"));
    tree.writeFile(QStringLiteral("Link to notes.txt")); // the name is taken

    ops.createLink({ tree.filePath("notes.txt") }, tree.path());
    QVERIFY(settle(ops));

    // The occupant is untouched; the link lands under the (copy) suffixing.
    QVERIFY(!QFileInfo(tree.filePath("Link to notes.txt")).isSymLink());
    QVERIFY(QFileInfo(tree.filePath("Link to notes (copy).txt")).isSymLink());
}

void TestFileOperations::undoOfCreateLinkRemovesOnlyTheLinks()
{
    TempTree tree;
    FileOperations ops;
    tree.writeFile(QStringLiteral("a.txt"));
    tree.writeFile(QStringLiteral("b.txt"));

    ops.createLink({ tree.filePath("a.txt"), tree.filePath("b.txt") }, tree.path());
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo(tree.filePath("Link to a.txt")).isSymLink());

    ops.undo();
    QVERIFY(settle(ops));

    // The links are gone; the targets were never the operation's to touch.
    QVERIFY(!QFileInfo::exists(tree.filePath("Link to a.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("Link to b.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("a.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("b.txt")));
    QVERIFY(!ops.canUndo());
}

void TestFileOperations::refusesToCopyAFolderIntoItself()
{
    TempTree tree;
    tree.writeFile("recursive/file.txt");
    FileOperations ops;

    // Without the guard this recurses until the disk fills.
    ops.copy({ tree.filePath("recursive") }, tree.filePath("recursive"));
    QVERIFY(settle(ops));

    QVERIFY2(!ops.lastError().isEmpty(), "copying a folder into itself must fail");
    QVERIFY(!QFileInfo::exists(tree.filePath("recursive/recursive/recursive")));
}

void TestFileOperations::deletePermanentlyLeavesNoUndo()
{
    TempTree tree;
    tree.writeFile("gone.txt");
    FileOperations ops;

    ops.deletePermanently({ tree.filePath("gone.txt") });
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("gone.txt")));
    // Offering undo here would be a lie the user only discovers when they need it.
    QVERIFY(!ops.canUndo());
}

void TestFileOperations::deleteRemovesATreeEntirely()
{
    TempTree tree;
    tree.writeFile("doomed/a.txt");
    tree.writeFile("doomed/deep/b.txt");
    tree.writeFile("survivor.txt");
    FileOperations ops;

    ops.deletePermanently({ tree.filePath("doomed") });
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("doomed")));
    QVERIFY(QFileInfo::exists(tree.filePath("survivor.txt")));
}

// A symlink inside a deleted folder must cost the link, never what it points
// at. Anything else deletes data the user never selected.
void TestFileOperations::deleteDoesNotFollowSymlinksOutOfTheTree()
{
    TempTree tree;
    tree.writeFile("outside/precious.txt");
    tree.makeDir("doomed");
    QVERIFY(QFile::link(tree.filePath("outside"), tree.filePath("doomed/link")));
    FileOperations ops;

    ops.deletePermanently({ tree.filePath("doomed") });
    QVERIFY(settle(ops));

    QVERIFY(!QFileInfo::exists(tree.filePath("doomed")));
    QVERIFY(QFileInfo::exists(tree.filePath("outside")));
    QVERIFY(QFileInfo::exists(tree.filePath("outside/precious.txt")));
}

// Copying a folder must copy its symlinks as symlinks. Dereferencing them
// silently inflates the copy and detaches it from what the user linked.
void TestFileOperations::copyPreservesSymlinksRatherThanDereferencingThem()
{
    TempTree tree;
    tree.writeFile("elsewhere/big.bin", 4096);
    tree.makeDir("source");
    QVERIFY(QFile::link(tree.filePath("elsewhere"), tree.filePath("source/link")));
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("source") }, tree.filePath("target"));
    QVERIFY(settle(ops));

    const QFileInfo copied(tree.filePath("target/source/link"));
    QVERIFY(copied.exists() || copied.isSymLink());
    QVERIFY2(copied.isSymLink(), "the symlink was dereferenced into a real directory");
}

// A folder that links to itself must not be walked forever.
void TestFileOperations::copySurvivesASymlinkLoop()
{
    TempTree tree;
    tree.writeFile("source/a.txt");
    QVERIFY(QFile::link(tree.filePath("source"), tree.filePath("source/loop")));
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("source") }, tree.filePath("target"));
    QVERIFY(settle(ops, 15000));
}

void TestFileOperations::reportsErrorsWithoutRecordingUndo()
{
    TempTree tree;
    FileOperations ops;

    ops.copy({ QStringLiteral("/nonexistent/source.txt") }, tree.path());
    QVERIFY(settle(ops));

    QVERIFY(!ops.lastError().isEmpty());
    // There is no reliable inverse for "half of it happened".
    QVERIFY(!ops.canUndo());
}

void TestFileOperations::operationsListRunningThenQueued()
{
    TempTree tree;
    tree.writeFile("a.txt", 3);
    tree.writeFile("b.txt", 3);
    tree.makeDir("target");
    FileOperations ops;

    // Synchronous window: the worker cannot report back until our event loop
    // spins, so right after two enqueues the first is running, the second
    // queued — deterministically.
    ops.copy({ tree.filePath("a.txt") }, tree.filePath("target"));
    ops.copy({ tree.filePath("b.txt") }, tree.filePath("target"));

    const QVariantList list = ops.operations();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value("state").toString(), QStringLiteral("running"));
    QCOMPARE(list.at(1).toMap().value("state").toString(), QStringLiteral("queued"));
    QVERIFY(!list.at(0).toMap().value("label").toString().isEmpty());
    // The sidebar indicator's live text: progressive, Nautilus-style.
    QVERIFY(list.at(0).toMap().value("shortStatus").toString()
                .startsWith(QStringLiteral("Copying")));
    QVERIFY(list.at(1).toMap().value("shortStatus").toString()
                .startsWith(QStringLiteral("Copying")));

    QVERIFY(settle(ops));
    QVERIFY(ops.operations().isEmpty());
}

void TestFileOperations::cancelDropsAQueuedOperation()
{
    TempTree tree;
    tree.writeFile("a.txt", 3);
    tree.writeFile("b.txt", 3);
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("a.txt") }, tree.filePath("target"));
    ops.copy({ tree.filePath("b.txt") }, tree.filePath("target"));

    const QVariantList list = ops.operations();
    QCOMPARE(list.size(), 2);
    ops.cancelOperation(list.at(1).toMap().value("id").toDouble());
    QCOMPARE(ops.operations().size(), 1);

    QVERIFY(settle(ops));
    // The first ran; the cancelled one never started.
    QVERIFY(QFileInfo::exists(tree.filePath("target/a.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("target/b.txt")));
}

void TestFileOperations::remainingTextEstimates()
{
    // Too early, no bytes, or done: no estimate is the only honest estimate.
    QCOMPARE(FileOperations::remainingText(0, 100, 5000), QString());
    QCOMPARE(FileOperations::remainingText(50, 100, 800), QString());
    QCOMPARE(FileOperations::remainingText(100, 100, 5000), QString());

    // Half done in 10s → about 10s left; a tenth done in 30s → minutes.
    QCOMPARE(FileOperations::remainingText(50, 100, 10000),
             QStringLiteral("About 11 seconds left"));
    QCOMPARE(FileOperations::remainingText(10, 100, 30000),
             QStringLiteral("About 5 minutes left"));
    QVERIFY(FileOperations::remainingText(99, 100, 10000)
                .contains(QStringLiteral("few seconds")));
}

void TestFileOperations::transferredTextFormats()
{
    // Nothing moved yet, or no total: no line at all.
    QCOMPARE(FileOperations::transferredText(0, 100, 5000), QString());
    QCOMPARE(FileOperations::transferredText(50, 0, 5000), QString());

    // Bytes appear as soon as they exist; the rate waits for the same 1.5s
    // the time estimate does.
    const QString early = FileOperations::transferredText(50, 100, 500);
    QVERIFY(early.contains(QStringLiteral(" / ")));
    QVERIFY(!early.contains(QStringLiteral("/s")));

    const QString steady = FileOperations::transferredText(50, 100, 2000);
    QVERIFY(steady.contains(QStringLiteral(" / ")));
    QVERIFY(steady.endsWith(QStringLiteral("/s)")));

    // A finished transfer shows totals, not a rate for work that is over.
    QVERIFY(!FileOperations::transferredText(100, 100, 5000)
                 .contains(QStringLiteral("/s")));
}

static bool makeLockedZip(TempTree &tree, const QString &name)
{
    QString error;
    return ArchiveEngine::compress({ tree.filePath("secret.txt") }, tree.filePath(name),
                                   &error, [] { return false; }, [](qint64, qint64) {},
                                   QStringLiteral("hunter2"));
}

void TestFileOperations::encryptedExtractAsksThenReplays()
{
    TempTree tree;
    tree.writeFile("secret.txt", 9);
    tree.makeDir("out");
    QVERIFY(makeLockedZip(tree, "locked.zip"));

    FileOperations ops;
    QSignalSpy asked(&ops, &FileOperations::passphraseNeeded);

    // Without a password the operation must park and ask — not error.
    ops.extractHere({ tree.filePath("locked.zip") }, tree.filePath("out"));
    QVERIFY(settle(ops));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.first().first().toString(), QStringLiteral("locked.zip"));
    QVERIFY(ops.lastError().isEmpty());

    // The answer replays the parked request and the files land.
    ops.providePassphrase(QStringLiteral("hunter2"));
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("out/secret.txt")));

    // A wrong answer asks again rather than failing.
    tree.makeDir("out2");
    ops.extractHere({ tree.filePath("locked.zip") }, tree.filePath("out2"));
    QVERIFY(settle(ops));
    QCOMPARE(asked.count(), 2);
    ops.providePassphrase(QStringLiteral("wrong"));
    QVERIFY(settle(ops));
    QCOMPARE(asked.count(), 3);
}

void TestFileOperations::decliningThePassphraseDropsTheRequest()
{
    TempTree tree;
    tree.writeFile("secret.txt", 9);
    tree.makeDir("out");
    QVERIFY(makeLockedZip(tree, "locked.zip"));

    FileOperations ops;
    ops.extractHere({ tree.filePath("locked.zip") }, tree.filePath("out"));
    QVERIFY(settle(ops));

    ops.declinePassphrase();
    // A late answer must be a no-op — the parked request is gone.
    ops.providePassphrase(QStringLiteral("hunter2"));
    QVERIFY(settle(ops));
    QVERIFY(QDir(tree.filePath("out"))
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
}

void TestFileOperations::redoReplaysAnUndoneCopy()
{
    TempTree tree;
    tree.writeFile("keep.txt", 7);
    tree.makeDir("target");
    FileOperations ops;

    ops.copy({ tree.filePath("keep.txt") }, tree.filePath("target"));
    QVERIFY(settle(ops));
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(!QFileInfo::exists(tree.filePath("target/keep.txt")));
    QVERIFY(ops.canRedo());

    ops.redo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("target/keep.txt")));
    QCOMPARE(QFileInfo(tree.filePath("target/keep.txt")).size(), 7);

    // The redo re-armed the undo: the same step walks back again.
    QVERIFY(ops.canUndo());
    QVERIFY(!ops.canRedo());
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(!QFileInfo::exists(tree.filePath("target/keep.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("keep.txt")));
}

void TestFileOperations::redoReproducesConflictRenames()
{
    TempTree tree;
    tree.writeFile("notes.txt", 5);
    tree.makeDir("target");
    tree.writeFile("target/notes.txt", 99);
    FileOperations ops;

    // The copy conflict-renames to "notes (copy).txt"; the redo must land on
    // the same name, because the same policy meets the same disk state.
    ops.copy({ tree.filePath("notes.txt") }, tree.filePath("target"));
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("target/notes (copy).txt")));

    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(!QFileInfo::exists(tree.filePath("target/notes (copy).txt")));

    ops.redo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("target/notes (copy).txt")));
    // The file that caused the conflict was never touched, either time.
    QCOMPARE(QFileInfo(tree.filePath("target/notes.txt")).size(), 99);
}

void TestFileOperations::redoOfRenameWalksBothWays()
{
    TempTree tree;
    tree.writeFile("old.txt", 3);
    FileOperations ops;

    ops.rename(tree.filePath("old.txt"), QStringLiteral("new.txt"));
    QVERIFY(settle(ops));
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("old.txt")));

    ops.redo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("new.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("old.txt")));
}

void TestFileOperations::freshOperationClearsTheRedoStack()
{
    TempTree tree;
    tree.writeFile("a.txt", 3);
    FileOperations ops;

    ops.rename(tree.filePath("a.txt"), QStringLiteral("b.txt"));
    QVERIFY(settle(ops));
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(ops.canRedo());

    // History forked: the world moved on, the undone rename is no longer
    // replayable.
    ops.createFolder(tree.path(), QStringLiteral("fresh"));
    QVERIFY(settle(ops));
    QVERIFY(!ops.canRedo());
}

void TestFileOperations::redoChainsThroughSeveralUndos()
{
    TempTree tree;
    tree.writeFile("one.txt", 3);
    FileOperations ops;

    ops.rename(tree.filePath("one.txt"), QStringLiteral("two.txt"));
    QVERIFY(settle(ops));
    ops.rename(tree.filePath("two.txt"), QStringLiteral("three.txt"));
    QVERIFY(settle(ops));

    ops.undo();
    QVERIFY(settle(ops));
    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("one.txt")));

    // Both undone steps replay, oldest first, each re-arming its undo.
    ops.redo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("two.txt")));
    ops.redo();
    QVERIFY(settle(ops));
    QVERIFY(QFileInfo::exists(tree.filePath("three.txt")));
    QVERIFY(!ops.canRedo());
    QVERIFY(ops.canUndo());
}

QTEST_GUILESS_MAIN(TestFileOperations)
#include "tst_fileoperations.moc"
