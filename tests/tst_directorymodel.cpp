#include "DirectoryModel.h"
#include "TestFixture.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <utime.h>

// These tests drive real files through real inotify, so they are slower than
// pure unit tests and worth every millisecond: the whole point of DirectoryModel
// is how it reacts to the filesystem changing underneath it.
class TestDirectoryModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // GLib criticals abort the suite — GIO getter misuse (asking a typed
    // getter for an absent attribute) must fail a test, not fill stderr.
    void initTestCase() { g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL); }

    void listsDirectoryContents();
    void exposesOwnershipAndTimes();
    void emptyDirectoryIsEmpty();
    void reportsErrorForMissingDirectory();
    void parsesFileAttributes();
    void toleratesSparseFileInfo();
    void insertsCreatedFileInPlace();
    void removesDeletedFile();
    void handlesRenameAsRemoveAndInsert();
    void doesNotResetModelOnFileChange();
    void coalescesEventStorms();
    void batchesBulkRemoval();
    void indexStaysCorrectThroughChurn();
    void reloadRereadsFromDisk();
    void switchingPathDiscardsPreviousLoad();
    void lookupHelpersAgreeWithRows();
    void countsDirectoryItems();
    void countsFollowLateEnable();
    void disablingCountsForgetsThem();
    void recountsWhenAFolderIsTouched();

private:
    // Names currently in the model, order-independent — DirectoryModel is
    // deliberately unsorted, so any test asserting order would be asserting
    // enumeration luck.
    static QStringList namesIn(const DirectoryModel &model)
    {
        QStringList names;
        for (int row = 0; row < model.rowCount(); ++row)
            names << model.data(model.index(row), DirectoryModel::NameRole).toString();
        names.sort();
        return names;
    }
};

void TestDirectoryModel::listsDirectoryContents()
{
    TempTree tree;
    tree.writeFile("alpha.txt");
    tree.writeFile("beta.txt");
    tree.makeDir("gamma");

    DirectoryModel model;
    model.setPath(tree.path());

    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(namesIn(model), (QStringList{ "alpha.txt", "beta.txt", "gamma" }));
    QCOMPARE(model.count(), 3);
    QVERIFY(!model.loading());
}

void TestDirectoryModel::exposesOwnershipAndTimes()
{
    TempTree tree;
    tree.writeFile("mine.txt");
    tree.makeDir("folder");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 2);

    auto valueFor = [&](const QString &name, int role) {
        const int row = model.indexOfName(name);
        return model.data(model.index(row), role);
    };

    // Local files always carry unix ownership; the columns show these as-is.
    QVERIFY(!valueFor("mine.txt", DirectoryModel::OwnerRole).toString().isEmpty());
    QVERIFY(!valueFor("mine.txt", DirectoryModel::GroupRole).toString().isEmpty());

    // ls-shaped permission strings, with the type character leading.
    const QRegularExpression shape(QStringLiteral("^[-dl][rwxsStT-]{9}$"));
    const QString filePerms = valueFor("mine.txt", DirectoryModel::PermissionsRole).toString();
    const QString dirPerms = valueFor("folder", DirectoryModel::PermissionsRole).toString();
    QVERIFY(shape.match(filePerms).hasMatch());
    QVERIFY(filePerms.startsWith(QLatin1Char('-')));
    QVERIFY(dirPerms.startsWith(QLatin1Char('d')));

    // Both filesystems this runs on report birth times; access always exists.
    QVERIFY(valueFor("mine.txt", DirectoryModel::CreatedRole).toDateTime().isValid());
    QVERIFY(valueFor("mine.txt", DirectoryModel::AccessedRole).toDateTime().isValid());
}

void TestDirectoryModel::emptyDirectoryIsEmpty()
{
    TempTree tree;

    DirectoryModel model;
    QSignalSpy loading(&model, &DirectoryModel::loadingChanged);
    model.setPath(tree.path());

    QTRY_VERIFY(!model.loading() && !loading.isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.errorMessage().isEmpty());
}

void TestDirectoryModel::reportsErrorForMissingDirectory()
{
    DirectoryModel model;
    model.setPath(QStringLiteral("/definitely/not/a/real/path/omanta"));

    QTRY_VERIFY(!model.errorMessage().isEmpty());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.loading());
}

void TestDirectoryModel::parsesFileAttributes()
{
    TempTree tree;
    tree.writeFile("visible.txt", 1234);
    tree.writeFile(".hidden");
    tree.writeFile("backup.txt~");
    tree.makeDir("folder");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 4);

    auto valueFor = [&](const QString &name, int role) {
        const int row = model.indexOfName(name);
        return model.data(model.index(row), role);
    };

    QCOMPARE(valueFor("visible.txt", DirectoryModel::SizeRole).toLongLong(), 1234);
    QCOMPARE(valueFor("visible.txt", DirectoryModel::IsDirRole).toBool(), false);
    QCOMPARE(valueFor("folder", DirectoryModel::IsDirRole).toBool(), true);
    QCOMPARE(valueFor(".hidden", DirectoryModel::IsHiddenRole).toBool(), true);
    QCOMPARE(valueFor("backup.txt~", DirectoryModel::IsBackupRole).toBool(), true);
    QCOMPARE(valueFor("visible.txt", DirectoryModel::IsHiddenRole).toBool(), false);

    // Content type drives both the Type column and the icon fallback chain.
    QVERIFY(!valueFor("visible.txt", DirectoryModel::ContentTypeRole).toString().isEmpty());
    QVERIFY(!valueFor("visible.txt", DirectoryModel::TypeDescriptionRole).toString().isEmpty());
    QVERIFY(valueFor("visible.txt", DirectoryModel::IconSourceRole)
                .toString().startsWith(QLatin1String("image://fileicon/")));
}

// Remote backends return infos missing attributes that were explicitly
// queried — the gphoto2 backend omits standard::is-{hidden,backup,symlink}
// (found with a real Canon R5 C, 2026-08-09). The typed getters CRITICAL on
// absence, and criticals are fatal here, so a regression aborts this test.
void TestDirectoryModel::toleratesSparseFileInfo()
{
    GFileInfo *info = g_file_info_new();
    g_file_info_set_name(info, "IMG_0001.CR3");
    g_file_info_set_file_type(info, G_FILE_TYPE_REGULAR);

    const FileEntry entry = FileEntry::fromInfo(info);
    g_object_unref(info);

    QCOMPARE(entry.name, QStringLiteral("IMG_0001.CR3"));
    QVERIFY(!entry.isHidden);
    QVERIFY(!entry.isBackup);
    QVERIFY(!entry.isSymlink);
    QCOMPARE(entry.size, qint64(0));
}

void TestDirectoryModel::insertsCreatedFileInPlace()
{
    TempTree tree;
    tree.writeFile("existing.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&model, &QAbstractItemModel::modelAboutToBeReset);

    tree.writeFile("appeared.txt");

    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(namesIn(model), (QStringList{ "appeared.txt", "existing.txt" }));
    QVERIFY(!inserted.isEmpty());
    QCOMPARE(reset.count(), 0); // an insert, not a reload
}

void TestDirectoryModel::removesDeletedFile()
{
    TempTree tree;
    tree.writeFile("keep.txt");
    tree.writeFile("doomed.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 2);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    tree.remove("doomed.txt");

    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(namesIn(model), (QStringList{ "keep.txt" }));
    QVERIFY(!removed.isEmpty());
    QCOMPARE(model.indexOfName(QStringLiteral("doomed.txt")), -1);
}

void TestDirectoryModel::handlesRenameAsRemoveAndInsert()
{
    TempTree tree;
    tree.writeFile("before.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);

    tree.rename("before.txt", "after.txt");

    QTRY_COMPARE(namesIn(model), (QStringList{ "after.txt" }));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.indexOfName(QStringLiteral("before.txt")), -1);
    QVERIFY(model.indexOfName(QStringLiteral("after.txt")) >= 0);
}

void TestDirectoryModel::doesNotResetModelOnFileChange()
{
    // The central claim of the design: the model updates in place. A reset
    // would drop selection and scroll position on every write to the folder.
    TempTree tree;
    tree.writeFile("one.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);

    QSignalSpy reset(&model, &QAbstractItemModel::modelAboutToBeReset);

    tree.writeFile("two.txt");
    QTRY_COMPARE(model.rowCount(), 2);
    tree.remove("one.txt");
    QTRY_COMPARE(model.rowCount(), 1);
    tree.writeFile("three.txt", 999);
    QTRY_COMPARE(model.rowCount(), 2);

    QCOMPARE(reset.count(), 0);
}

void TestDirectoryModel::coalescesEventStorms()
{
    TempTree tree;

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_VERIFY(!model.loading());

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelAboutToBeReset);

    // Twenty files written back to back. Each one produces CREATED + CHANGED +
    // CHANGES_DONE, so roughly 60 monitor events arrive inside the settle
    // window.
    constexpr int fileCount = 20;
    for (int i = 0; i < fileCount; ++i)
        tree.writeFile(QStringLiteral("burst%1.txt").arg(i));

    QTRY_COMPARE(model.rowCount(), fileCount);

    // What coalescing actually guarantees: each *file* is queried and applied
    // once, however many events it generated. Sixty events must not become
    // sixty model updates, and must never become a reload.
    QCOMPARE(reset.count(), 0);
    QVERIFY2(changed.count() <= fileCount,
             qPrintable(QStringLiteral("%1 dataChanged signals for %2 files — the CHANGED and "
                                       "CHANGES_DONE follow-ups are not being absorbed")
                            .arg(changed.count()).arg(fileCount)));

    // Arrivals within a settle window are applied as one insertion. A signal
    // per file would mean the sort proxy and the view redo their work once per
    // file, which is what makes a large paste crawl. A couple of batches is
    // expected — the writes may straddle a settle boundary — but nothing close
    // to one per file.
    QVERIFY2(inserted.count() <= 3,
             qPrintable(QStringLiteral("%1 insert signals for %2 files — arrivals are not "
                                       "being batched")
                            .arg(inserted.count()).arg(fileCount)));
}

void TestDirectoryModel::batchesBulkRemoval()
{
    TempTree tree;
    constexpr int fileCount = 20;
    for (int i = 0; i < fileCount; ++i)
        tree.writeFile(QStringLiteral("doomed%1.txt").arg(i));
    tree.writeFile(QStringLiteral("survivor.txt"));

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), fileCount + 1);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&model, &QAbstractItemModel::modelAboutToBeReset);

    for (int i = 0; i < fileCount; ++i)
        tree.remove(QStringLiteral("doomed%1.txt").arg(i));

    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(namesIn(model), (QStringList{ "survivor.txt" }));
    QCOMPARE(reset.count(), 0);

    // Adjacent rows come out as spans, and the name index is rebuilt once per
    // batch rather than once per file — deleting n files was O(n^2).
    QVERIFY2(removed.count() <= 3,
             qPrintable(QStringLiteral("%1 removal signals for %2 files — removals are not "
                                       "being batched")
                            .arg(removed.count()).arg(fileCount)));

    // The index must still be correct after a batched removal, or every later
    // lookup silently targets the wrong row.
    QCOMPARE(model.indexOfName(QStringLiteral("survivor.txt")), 0);
    for (int i = 0; i < fileCount; ++i)
        QCOMPARE(model.indexOfName(QStringLiteral("doomed%1.txt").arg(i)), -1);
}

void TestDirectoryModel::indexStaysCorrectThroughChurn()
{
    // Interleaved creates and deletes are where an incrementally-maintained
    // index drifts out of step with the rows. Phase 3 will act on the row a
    // lookup returns, so drift here means operating on the wrong file.
    TempTree tree;
    for (int i = 0; i < 10; ++i)
        tree.writeFile(QStringLiteral("f%1.txt").arg(i));

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 10);

    for (int round = 0; round < 3; ++round) {
        tree.remove(QStringLiteral("f%1.txt").arg(round * 3));
        tree.remove(QStringLiteral("f%1.txt").arg(round * 3 + 1));
        tree.writeFile(QStringLiteral("new%1.txt").arg(round));
        QTRY_COMPARE(model.rowCount(), 10 - (round + 1) * 2 + (round + 1));
    }

    // Every name must resolve to the row that actually holds it.
    for (int row = 0; row < model.rowCount(); ++row) {
        const QString name = model.data(model.index(row), DirectoryModel::NameRole).toString();
        QCOMPARE(model.indexOfName(name), row);
    }
    QCOMPARE(model.indexOfName(QStringLiteral("f0.txt")), -1);
}

void TestDirectoryModel::reloadRereadsFromDisk()
{
    TempTree tree;
    tree.writeFile("first.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);

    model.reload();
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(namesIn(model), (QStringList{ "first.txt" }));
}

void TestDirectoryModel::switchingPathDiscardsPreviousLoad()
{
    // The generation guard: results from a superseded load must not leak into
    // the model, or navigating quickly leaves two directories mixed together.
    TempTree first;
    for (int i = 0; i < 40; ++i)
        first.writeFile(QStringLiteral("first%1.txt").arg(i));

    TempTree second;
    second.writeFile("only.txt");

    DirectoryModel model;
    model.setPath(first.path());
    model.setPath(second.path()); // immediately, before the first load finishes

    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(namesIn(model), (QStringList{ "only.txt" }));
    QCOMPARE(model.path(), second.path());

    // Give any stray callback from the first load time to arrive and be dropped.
    QTest::qWait(300);
    QCOMPARE(model.rowCount(), 1);
}

void TestDirectoryModel::lookupHelpersAgreeWithRows()
{
    TempTree tree;
    tree.writeFile("a.txt");
    tree.writeFile("b.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 2);

    for (int row = 0; row < model.rowCount(); ++row) {
        const QString name = model.data(model.index(row), DirectoryModel::NameRole).toString();
        QCOMPARE(model.indexOfName(name), row);
        QCOMPARE(model.filePathAt(row), tree.filePath(name));
        QCOMPARE(model.data(model.index(row), DirectoryModel::FilePathRole).toString(),
                 tree.filePath(name));
    }

    QCOMPARE(model.indexOfName(QStringLiteral("nope.txt")), -1);
    QVERIFY(model.filePathAt(-1).isEmpty());
    QVERIFY(model.filePathAt(99).isEmpty());
}

void TestDirectoryModel::countsDirectoryItems()
{
    TempTree tree;
    tree.writeFile("stuff/a.txt");
    tree.writeFile("stuff/b.txt");
    tree.writeFile("stuff/c.txt");
    tree.writeFile("stuff/.secret");
    tree.writeFile("stuff/note.txt~");
    tree.writeFile("plain.txt");

    DirectoryModel model;
    model.setCountItems(true);
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 2);

    const auto countFor = [&](const QString &name, int role) {
        return model.data(model.index(model.indexOfName(name)), role).toInt();
    };

    // Nautilus's rule: the visible tally skips hidden and backup children,
    // the full tally takes everything — both from one enumeration.
    QTRY_COMPARE(countFor("stuff", DirectoryModel::ItemCountRole), 3);
    QCOMPARE(countFor("stuff", DirectoryModel::ItemCountAllRole), 5);

    // Files never carry a count.
    QCOMPARE(countFor("plain.txt", DirectoryModel::ItemCountRole), -1);
    QCOMPARE(countFor("plain.txt", DirectoryModel::ItemCountAllRole), -1);
}

void TestDirectoryModel::countsFollowLateEnable()
{
    TempTree tree;
    tree.writeFile("folder/one.txt");

    DirectoryModel model;
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);

    // Counting off: the count stays unknown however long we wait.
    QTest::qWait(150);
    QCOMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), -1);

    // Flipping the preference on counts the folders already listed.
    model.setCountItems(true);
    QTRY_COMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), 1);
}

void TestDirectoryModel::disablingCountsForgetsThem()
{
    TempTree tree;
    tree.writeFile("folder/one.txt");

    DirectoryModel model;
    model.setCountItems(true);
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);
    QTRY_COMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), 1);

    // Off again: display and sorting must stop claiming numbers the
    // preference has disavowed.
    model.setCountItems(false);
    QCOMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), -1);
    QCOMPARE(model.data(model.index(0), DirectoryModel::ItemCountAllRole).toInt(), -1);
}

void TestDirectoryModel::recountsWhenAFolderIsTouched()
{
    TempTree tree;
    tree.writeFile("folder/one.txt");

    DirectoryModel model;
    model.setCountItems(true);
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 1);
    QTRY_COMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), 1);

    // A grandchild's creation fires no event on this watch (inotify does not
    // look inside subdirectories) — but anything that makes the folder's own
    // entry refresh must recount it. Touch stands in for the real-world
    // triggers: a paste into it, chmod, a mover updating mtimes.
    tree.writeFile("folder/two.txt");
    utime(qPrintable(tree.filePath("folder")), nullptr);

    QTRY_COMPARE(model.data(model.index(0), DirectoryModel::ItemCountRole).toInt(), 2);
}

QTEST_GUILESS_MAIN(TestDirectoryModel)
#include "tst_directorymodel.moc"
