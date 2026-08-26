#include "BatchRenamer.h"
#include "FileOperations.h"
#include "TestFixture.h"

#include <QSignalSpy>
#include <QTest>

#include <gio/gio.h>

// Batch rename in two halves: the BatchRenamer engine (pure name generation
// and conflict detection, no disk) and the BatchRename operation (real
// renames, one undo, order-safety through temp names, rollback on failure).
class TestBatchRename : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL); }

    // engine: template mode
    void defaultTemplateChangesNothing();
    void templateNumbersInNameOrder();
    void templatePadsNumbering();
    void templateKeepsTheExtension();
    void numberingFollowsModifiedOrder();
    void extensionOffsetEdgeCases();

    // engine: find/replace mode
    void findReplaceActsOnTheFullName();
    void emptyFindChangesNothing();

    // engine: conflicts
    void conflictWithAFileKeepingItsName();
    void conflictWithinTheBatch();
    void swapIsNotAConflict();
    void emptyResultingNameIsInvalid();
    void insertTagInsertsAtTheCursor();

    // the operation
    void renamesAllOnDisk();
    void oneUndoRestoresEveryName();
    void shiftedNamesGoThroughTempNames();
    void swappedNamesExchangeContents();
    void failureRollsEveryNameBack();
    void refusesMismatchedLists();

private:
    static QVariantMap item(const QString &path, const QString &name,
                            const QDateTime &modified = QDateTime(), bool isDir = false)
    {
        return { { QStringLiteral("path"), path },
                 { QStringLiteral("name"), name },
                 { QStringLiteral("modified"), modified },
                 { QStringLiteral("isDir"), isDir } };
    }

    static QStringList newNamesOf(const BatchRenamer &renamer)
    {
        QStringList names;
        const QVariantList preview = renamer.preview();
        for (const QVariant &row : preview)
            names << row.toMap().value(QStringLiteral("newName")).toString();
        return names;
    }

    static bool settle(FileOperations &ops, int timeoutMs = 10000)
    {
        QElapsedTimer timer;
        timer.start();
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

    static QString write(const TempTree &tree, const QString &name, const QByteArray &content)
    {
        const QString target = tree.filePath(name);
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(content);
        return target;
    }
};

void TestBatchRename::defaultTemplateChangesNothing()
{
    BatchRenamer renamer;
    renamer.setSelection({ item("/t/a.txt", "a.txt"), item("/t/b.txt", "b.txt") }, {});

    QCOMPARE(newNamesOf(renamer), QStringList({ "a.txt", "b.txt" }));
    // Nothing changes, so there is nothing to rename.
    QVERIFY(!renamer.canRename());
}

void TestBatchRename::templateNumbersInNameOrder()
{
    BatchRenamer renamer;
    // Deliberately out of order: numbering must follow the chosen order (name
    // ascending by default), not the order the selection arrived in.
    renamer.setSelection({ item("/t/c.jpg", "c.jpg"), item("/t/a.jpg", "a.jpg"),
                           item("/t/b.jpg", "b.jpg") },
                         {});
    renamer.setTemplateText(QStringLiteral("Photo [1, 2, 3]"));

    QCOMPARE(newNamesOf(renamer),
             QStringList({ "Photo 1.jpg", "Photo 2.jpg", "Photo 3.jpg" }));
    QVERIFY(renamer.canRename());
    QVERIFY(renamer.hasNumbering());

    renamer.setNumberingOrder(BatchRenamer::NameDescending);
    // Same names — but c.jpg is now number 1, and the preview shows c first.
    QCOMPARE(renamer.sourcePaths().first(), QStringLiteral("/t/c.jpg"));
    QCOMPARE(renamer.newNames().first(), QStringLiteral("Photo 1.jpg"));
}

void TestBatchRename::templatePadsNumbering()
{
    BatchRenamer renamer;
    renamer.setSelection({ item("/t/a.txt", "a.txt") }, {});

    renamer.setTemplateText(QStringLiteral("x [01, 02, 03]"));
    QCOMPARE(newNamesOf(renamer), QStringList({ "x 01.txt" }));

    renamer.setTemplateText(QStringLiteral("x [001, 002, 003]"));
    QCOMPARE(newNamesOf(renamer), QStringList({ "x 001.txt" }));
}

void TestBatchRename::templateKeepsTheExtension()
{
    BatchRenamer renamer;
    renamer.setSelection({ item("/t/notes.txt", "notes.txt"),
                           item("/t/stuff", "stuff", QDateTime(), true) },
                         {});
    renamer.setTemplateText(QStringLiteral("[Original file name] copy"));

    // The template names the stem; the extension survives untouched. A
    // directory has no extension, so the template names the whole thing.
    QCOMPARE(newNamesOf(renamer), QStringList({ "notes copy.txt", "stuff copy" }));
}

void TestBatchRename::numberingFollowsModifiedOrder()
{
    BatchRenamer renamer;
    const QDateTime older = QDateTime::fromSecsSinceEpoch(1000);
    const QDateTime newer = QDateTime::fromSecsSinceEpoch(2000);
    renamer.setSelection({ item("/t/a.jpg", "a.jpg", newer), item("/t/b.jpg", "b.jpg", older) },
                         {});
    renamer.setTemplateText(QStringLiteral("n[1, 2, 3]"));
    renamer.setNumberingOrder(BatchRenamer::FirstModified);

    // b is older, so b gets 1.
    QCOMPARE(renamer.sourcePaths(), QStringList({ "/t/b.jpg", "/t/a.jpg" }));
    QCOMPARE(renamer.newNames(), QStringList({ "n1.jpg", "n2.jpg" }));

    renamer.setNumberingOrder(BatchRenamer::LastModified);
    QCOMPARE(renamer.sourcePaths(), QStringList({ "/t/a.jpg", "/t/b.jpg" }));
}

void TestBatchRename::extensionOffsetEdgeCases()
{
    QCOMPARE(BatchRenamer::extensionOffset("notes.txt", false), 5);
    // A leading dot is a hidden file's name, not an extension.
    QCOMPARE(BatchRenamer::extensionOffset(".bashrc", false), -1);
    QCOMPARE(BatchRenamer::extensionOffset("plain", false), -1);
    // Compound archive extensions rename as one unit.
    QCOMPARE(BatchRenamer::extensionOffset("backup.tar.gz", false), 6);
    // Directories never have one.
    QCOMPARE(BatchRenamer::extensionOffset("dir.d", true), -1);
}

void TestBatchRename::findReplaceActsOnTheFullName()
{
    BatchRenamer renamer;
    renamer.setMode(BatchRenamer::FindReplace);
    renamer.setSelection({ item("/t/IMG_1.jpg", "IMG_1.jpg") }, {});
    renamer.setFindText(QStringLiteral("IMG"));
    renamer.setReplaceText(QStringLiteral("Photo"));

    QCOMPARE(newNamesOf(renamer), QStringList({ "Photo_1.jpg" }));

    // Unlike template mode, find/replace sees the extension too — that is how
    // Nautilus lets ".jpeg" become ".jpg".
    renamer.setFindText(QStringLiteral(".jpg"));
    renamer.setReplaceText(QStringLiteral(".jpeg"));
    QCOMPARE(newNamesOf(renamer), QStringList({ "IMG_1.jpeg" }));
}

void TestBatchRename::emptyFindChangesNothing()
{
    BatchRenamer renamer;
    renamer.setMode(BatchRenamer::FindReplace);
    renamer.setSelection({ item("/t/a.txt", "a.txt") }, {});
    renamer.setReplaceText(QStringLiteral("something"));

    QCOMPARE(newNamesOf(renamer), QStringList({ "a.txt" }));
    QVERIFY(!renamer.canRename());
}

void TestBatchRename::conflictWithAFileKeepingItsName()
{
    BatchRenamer renamer;
    renamer.setMode(BatchRenamer::FindReplace);
    // "taken.txt" exists in the folder and is not part of the batch.
    renamer.setSelection({ item("/t/a.txt", "a.txt") }, { "a.txt", "taken.txt" });
    renamer.setFindText(QStringLiteral("a"));
    renamer.setReplaceText(QStringLiteral("taken"));

    QVERIFY(!renamer.canRename());
    QVERIFY(!renamer.problem().isEmpty());
    QVERIFY(renamer.preview().first().toMap().value("conflict").toBool());
}

void TestBatchRename::conflictWithinTheBatch()
{
    BatchRenamer renamer;
    renamer.setSelection({ item("/t/a.jpg", "a.jpg"), item("/t/b.jpg", "b.jpg") },
                         { "a.jpg", "b.jpg" });
    renamer.setTemplateText(QStringLiteral("same"));

    // Both become "same.jpg".
    QVERIFY(!renamer.canRename());
    QVERIFY(!renamer.problem().isEmpty());
}

void TestBatchRename::swapIsNotAConflict()
{
    // A target held by a batch member that is renaming AWAY is not a
    // conflict — that vacated-name rule is what makes shifts executable.
    // photo1→photo11 while photo11→photo111: the "photo11.jpg" slot is being
    // vacated, so nothing collides.
    BatchRenamer renamer;
    renamer.setMode(BatchRenamer::FindReplace);
    renamer.setSelection({ item("/t/photo1.jpg", "photo1.jpg"),
                           item("/t/photo11.jpg", "photo11.jpg") },
                         { "photo1.jpg", "photo11.jpg" });
    renamer.setFindText(QStringLiteral("photo1"));
    renamer.setReplaceText(QStringLiteral("photo11"));

    QCOMPARE(newNamesOf(renamer), QStringList({ "photo11.jpg", "photo111.jpg" }));
    QVERIFY(renamer.canRename());

    // Whereas the same target held by a file KEEPING its name conflicts:
    // n1→n2 while n2 stays n2 (it contains no "n1").
    BatchRenamer clash;
    clash.setMode(BatchRenamer::FindReplace);
    clash.setSelection({ item("/t/n1", "n1"), item("/t/n2", "n2") }, { "n1", "n2" });
    clash.setFindText(QStringLiteral("n1"));
    clash.setReplaceText(QStringLiteral("n2"));
    QVERIFY(!clash.canRename());
}

void TestBatchRename::emptyResultingNameIsInvalid()
{
    BatchRenamer renamer;
    renamer.setMode(BatchRenamer::FindReplace);
    renamer.setSelection({ item("/t/a", "a") }, { "a" });
    renamer.setFindText(QStringLiteral("a"));
    renamer.setReplaceText(QString());

    QVERIFY(!renamer.canRename());
    QVERIFY(!renamer.problem().isEmpty());
}

void TestBatchRename::insertTagInsertsAtTheCursor()
{
    BatchRenamer renamer;
    QCOMPARE(renamer.insertTag(QStringLiteral("ab"), 1, QStringLiteral("[1, 2, 3]")),
             QStringLiteral("a[1, 2, 3]b"));
    QCOMPARE(renamer.insertTag(QString(), 5, QStringLiteral("x")), QStringLiteral("x"));
}

void TestBatchRename::renamesAllOnDisk()
{
    TempTree tree;
    tree.writeFile("a.txt");
    tree.writeFile("b.txt");
    FileOperations ops;

    ops.batchRename({ tree.filePath("a.txt"), tree.filePath("b.txt") },
                    { QStringLiteral("one.txt"), QStringLiteral("two.txt") });
    QVERIFY(settle(ops));

    QVERIFY(ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("one.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("two.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("a.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("b.txt")));
}

void TestBatchRename::oneUndoRestoresEveryName()
{
    TempTree tree;
    tree.writeFile("a.txt");
    tree.writeFile("b.txt");
    tree.writeFile("c.txt");
    FileOperations ops;

    ops.batchRename({ tree.filePath("a.txt"), tree.filePath("b.txt"), tree.filePath("c.txt") },
                    { QStringLiteral("x1.txt"), QStringLiteral("x2.txt"),
                      QStringLiteral("x3.txt") });
    QVERIFY(settle(ops));
    QVERIFY(ops.canUndo());

    ops.undo();
    QVERIFY(settle(ops));

    QVERIFY(QFileInfo::exists(tree.filePath("a.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("b.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("c.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("x1.txt")));
    // One batch, one undo — the stack is empty again.
    QVERIFY(!ops.canUndo());
}

void TestBatchRename::shiftedNamesGoThroughTempNames()
{
    TempTree tree;
    write(tree, "1.jpg", "first");
    write(tree, "2.jpg", "second");
    FileOperations ops;

    // 1→2 while 2→3: naive in-order renaming would clobber or fail on the
    // still-occupied "2.jpg". The two-phase pass must make this just work.
    ops.batchRename({ tree.filePath("1.jpg"), tree.filePath("2.jpg") },
                    { QStringLiteral("2.jpg"), QStringLiteral("3.jpg") });
    QVERIFY(settle(ops));

    QVERIFY(ops.lastError().isEmpty());
    QCOMPARE(read(tree.filePath("2.jpg")), QStringLiteral("first"));
    QCOMPARE(read(tree.filePath("3.jpg")), QStringLiteral("second"));
    QVERIFY(!QFileInfo::exists(tree.filePath("1.jpg")));
}

void TestBatchRename::swappedNamesExchangeContents()
{
    TempTree tree;
    write(tree, "a.txt", "was-a");
    write(tree, "b.txt", "was-b");
    FileOperations ops;

    ops.batchRename({ tree.filePath("a.txt"), tree.filePath("b.txt") },
                    { QStringLiteral("b.txt"), QStringLiteral("a.txt") });
    QVERIFY(settle(ops));

    QVERIFY(ops.lastError().isEmpty());
    QCOMPARE(read(tree.filePath("b.txt")), QStringLiteral("was-a"));
    QCOMPARE(read(tree.filePath("a.txt")), QStringLiteral("was-b"));

    ops.undo();
    QVERIFY(settle(ops));
    QCOMPARE(read(tree.filePath("a.txt")), QStringLiteral("was-a"));
    QCOMPARE(read(tree.filePath("b.txt")), QStringLiteral("was-b"));
}

void TestBatchRename::failureRollsEveryNameBack()
{
    TempTree tree;
    tree.writeFile("real1.txt");
    tree.writeFile("real2.txt");
    FileOperations ops;

    // The middle source does not exist, so the batch fails after real1 has
    // already been renamed — and real1's rename must be rolled back.
    ops.batchRename({ tree.filePath("real1.txt"), tree.filePath("missing.txt"),
                      tree.filePath("real2.txt") },
                    { QStringLiteral("n1.txt"), QStringLiteral("n2.txt"),
                      QStringLiteral("n3.txt") });
    QVERIFY(settle(ops));

    QVERIFY(!ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("real1.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("real2.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("n1.txt")));
    QVERIFY(!QFileInfo::exists(tree.filePath("n3.txt")));
    // A failed operation records no undo.
    QVERIFY(!ops.canUndo());
}

void TestBatchRename::refusesMismatchedLists()
{
    TempTree tree;
    tree.writeFile("a.txt");
    FileOperations ops;

    ops.batchRename({ tree.filePath("a.txt") }, { QStringLiteral("x"), QStringLiteral("y") });
    QVERIFY(!ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("a.txt")));

    ops.clearError();
    ops.batchRename({ tree.filePath("a.txt") }, { QStringLiteral("bad/name") });
    QVERIFY(!ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("a.txt")));
}

QTEST_GUILESS_MAIN(TestBatchRename)
#include "tst_batchrename.moc"
