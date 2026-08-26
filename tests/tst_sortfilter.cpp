#include "DirectoryModel.h"
#include "FileSortFilterModel.h"
#include "TestFixture.h"

#include <QTest>

// Ordering and visibility. These are the rules a user notices instantly when
// they are wrong — "file10 before file9" reads as broken even though nothing
// crashed.
class TestSortFilter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hidesHiddenAndBackupFilesByDefault();
    void showHiddenRevealsThem();
    void foldersSortFirst();
    void foldersStayFirstWhenReversed();
    void sortsNamesNaturally();
    void sortsCaseInsensitively();
    void sortsBySize();
    void sizeSortGroupsFoldersByItemCount();
    void sizeSortCountFollowsHiddenToggle();
    void sortsByModified();
    void sortsByCreated();
    void sortsByPermissions();
    void ownerSortFallsBackToName();
    void tieBreaksOnName();
    void nameFilterMatchesSubstrings();
    void foldersOnlyHidesFiles();
    void proxyRowForNameRoundTrips();
    void findByPrefixWrapsAround();
    void valueAtReadsNamedRoles();

private:
    // Visible names in proxy order — the order the user actually sees.
    static QStringList visible(const FileSortFilterModel &proxy)
    {
        QStringList names;
        for (int row = 0; row < proxy.rowCount(); ++row)
            names << proxy.valueAt(row, QStringLiteral("name")).toString();
        return names;
    }
};

void TestSortFilter::hidesHiddenAndBackupFilesByDefault()
{
    TempTree tree;
    tree.writeFile("visible.txt");
    tree.writeFile(".hidden");
    tree.writeFile("dropping.txt~");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());

    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(visible(proxy), (QStringList{ "visible.txt" }));
    QCOMPARE(proxy.count(), 1);
}

void TestSortFilter::showHiddenRevealsThem()
{
    TempTree tree;
    tree.writeFile("visible.txt");
    tree.writeFile(".hidden");
    tree.writeFile("dropping.txt~");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(model.rowCount(), 3);

    proxy.setShowHidden(true);
    QCOMPARE(proxy.rowCount(), 3);

    // Toggling back must not require re-reading the directory.
    proxy.setShowHidden(false);
    QCOMPARE(proxy.rowCount(), 1);
}

void TestSortFilter::foldersSortFirst()
{
    TempTree tree;
    tree.writeFile("aaa.txt");
    tree.makeDir("zzz");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());

    QTRY_COMPARE(proxy.rowCount(), 2);
    QCOMPARE(visible(proxy), (QStringList{ "zzz", "aaa.txt" }));
}

void TestSortFilter::foldersStayFirstWhenReversed()
{
    TempTree tree;
    tree.writeFile("aaa.txt");
    tree.writeFile("bbb.txt");
    tree.makeDir("folder");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setSortDescending(true);

    // Reversing sorts the files, but must not fling the folders to the bottom.
    QCOMPARE(visible(proxy), (QStringList{ "folder", "bbb.txt", "aaa.txt" }));
}

void TestSortFilter::sortsNamesNaturally()
{
    TempTree tree;
    for (const QString &name : { "file1.txt", "file2.txt", "file9.txt", "file10.txt", "file20.txt" })
        tree.writeFile(name);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());

    QTRY_COMPARE(proxy.rowCount(), 5);
    QCOMPARE(visible(proxy),
             (QStringList{ "file1.txt", "file2.txt", "file9.txt", "file10.txt", "file20.txt" }));
}

void TestSortFilter::sortsCaseInsensitively()
{
    TempTree tree;
    for (const QString &name : { "Apple", "banana", "Cherry", "date" })
        tree.writeFile(name);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());

    QTRY_COMPARE(proxy.rowCount(), 4);
    // Not "Apple, Cherry, banana, date" — case must not split the alphabet.
    QCOMPARE(visible(proxy), (QStringList{ "Apple", "banana", "Cherry", "date" }));
}

void TestSortFilter::sortsBySize()
{
    TempTree tree;
    tree.writeFile("small.txt", 10);
    tree.writeFile("medium.txt", 500);
    tree.writeFile("large.txt", 5000);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setSortKey(FileSortFilterModel::BySize);
    QCOMPARE(visible(proxy), (QStringList{ "small.txt", "medium.txt", "large.txt" }));

    proxy.setSortDescending(true);
    QCOMPARE(visible(proxy), (QStringList{ "large.txt", "medium.txt", "small.txt" }));
}

void TestSortFilter::sizeSortGroupsFoldersByItemCount()
{
    TempTree tree;
    tree.writeFile("many/a.txt");
    tree.writeFile("many/b.txt");
    tree.writeFile("many/c.txt");
    tree.writeFile("few/only.txt");
    tree.writeFile("huge.txt", 5000);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    // The grouping under test is compare_by_size's own, not folders-first —
    // which is what keeps folders together even when that preference is off.
    proxy.setFoldersFirst(false);
    model.setCountItems(true);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    // Nautilus's compare_by_size: folders group before files whatever their
    // on-disk size claims, ordered by how many items they hold. QTRY: the
    // counts arrive asynchronously and re-sort the proxy as they land.
    proxy.setSortKey(FileSortFilterModel::BySize);
    QTRY_COMPARE(visible(proxy), (QStringList{ "few", "many", "huge.txt" }));

    proxy.setSortDescending(true);
    QCOMPARE(visible(proxy), (QStringList{ "huge.txt", "many", "few" }));
}

void TestSortFilter::sizeSortCountFollowsHiddenToggle()
{
    TempTree tree;
    tree.writeFile("shadows/.one");
    tree.writeFile("shadows/.two");
    tree.writeFile("shadows/.three");
    tree.writeFile("open/visible.txt");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setCountItems(true);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 2);

    proxy.setSortKey(FileSortFilterModel::BySize);

    // Counting what the user can see: shadows holds 0 visible items, open
    // holds 1 — so shadows leads while hidden files are hidden…
    QTRY_COMPARE(visible(proxy), (QStringList{ "shadows", "open" }));

    // …and flips once Ctrl+H reveals its three, without touching the disk.
    proxy.setShowHidden(true);
    QTRY_COMPARE(visible(proxy), (QStringList{ "open", "shadows" }));
}

void TestSortFilter::sortsByModified()
{
    TempTree tree;
    tree.writeFile("oldest.txt");
    tree.writeFile("middle.txt");
    tree.writeFile("newest.txt");

    const QDateTime base = QDateTime::currentDateTime().addDays(-10);
    tree.setModified("oldest.txt", base);
    tree.setModified("middle.txt", base.addDays(3));
    tree.setModified("newest.txt", base.addDays(6));

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setSortKey(FileSortFilterModel::ByModified);
    QCOMPARE(visible(proxy), (QStringList{ "oldest.txt", "middle.txt", "newest.txt" }));
}

void TestSortFilter::sortsByCreated()
{
    TempTree tree;
    // Created in reverse-alphabetical order, milliseconds apart — the *_USEC
    // attributes give creation time sub-second precision, so the gaps hold.
    for (const QString &name : { "zulu.txt", "mike.txt", "alpha.txt" }) {
        tree.writeFile(name);
        QTest::qWait(20);
    }

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setSortKey(FileSortFilterModel::ByCreated);
    QCOMPARE(visible(proxy), (QStringList{ "zulu.txt", "mike.txt", "alpha.txt" }));
}

void TestSortFilter::sortsByPermissions()
{
    TempTree tree;
    tree.writeFile("secret.txt");
    tree.writeFile("shared.txt");
    QFile::setPermissions(tree.filePath("secret.txt"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::setPermissions(tree.filePath("shared.txt"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner
                          | QFileDevice::ReadGroup | QFileDevice::ReadOther);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 2);

    proxy.setSortKey(FileSortFilterModel::ByPermissions);
    // "-rw-------" < "-rw-r--r--" as plain strings.
    QCOMPARE(visible(proxy), (QStringList{ "secret.txt", "shared.txt" }));

    proxy.setSortDescending(true);
    QCOMPARE(visible(proxy), (QStringList{ "shared.txt", "secret.txt" }));
}

void TestSortFilter::ownerSortFallsBackToName()
{
    TempTree tree;
    tree.writeFile("charlie.txt");
    tree.writeFile("alpha.txt");
    tree.writeFile("bravo.txt");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    // One user owns the whole fixture, so every comparison is a tie and the
    // name tie-break must keep the order stable and alphabetical.
    proxy.setSortKey(FileSortFilterModel::ByOwner);
    QCOMPARE(visible(proxy), (QStringList{ "alpha.txt", "bravo.txt", "charlie.txt" }));

    proxy.setSortKey(FileSortFilterModel::ByGroup);
    QCOMPARE(visible(proxy), (QStringList{ "alpha.txt", "bravo.txt", "charlie.txt" }));
}

void TestSortFilter::tieBreaksOnName()
{
    TempTree tree;
    for (const QString &name : { "charlie.txt", "alpha.txt", "bravo.txt" })
        tree.writeFile(name, 100); // identical sizes

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setSortKey(FileSortFilterModel::BySize);

    // Equal sizes must still produce a stable, predictable order rather than
    // whatever the enumerator happened to return.
    QCOMPARE(visible(proxy), (QStringList{ "alpha.txt", "bravo.txt", "charlie.txt" }));
}

void TestSortFilter::nameFilterMatchesSubstrings()
{
    TempTree tree;
    tree.writeFile("report-2026.pdf");
    tree.writeFile("REPORT-draft.txt");
    tree.writeFile("invoice.txt");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    proxy.setNameFilter(QStringLiteral("report"));
    QCOMPARE(proxy.rowCount(), 2); // case-insensitive

    proxy.setNameFilter(QString());
    QCOMPARE(proxy.rowCount(), 3);
}

void TestSortFilter::foldersOnlyHidesFiles()
{
    TempTree tree;
    tree.writeFile("paper.txt");
    tree.makeDir("box");
    tree.makeDir("crate");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    proxy.setFoldersOnly(true);
    model.setPath(tree.path());

    // The folder picker's view: directories and nothing else.
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(visible(proxy), (QStringList{ "box", "crate" }));

    proxy.setFoldersOnly(false);
    QCOMPARE(proxy.count(), 3);
}

void TestSortFilter::proxyRowForNameRoundTrips()
{
    TempTree tree;
    tree.writeFile("aaa.txt");
    tree.writeFile("bbb.txt");
    tree.makeDir("folder");

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    for (int row = 0; row < proxy.rowCount(); ++row) {
        const QString name = proxy.valueAt(row, QStringLiteral("name")).toString();
        QCOMPARE(proxy.proxyRowForName(name), row);
        QVERIFY(proxy.sourceRow(row) >= 0);
    }

    // A hidden file has no proxy row even though the source model holds it.
    QCOMPARE(proxy.proxyRowForName(QStringLiteral("missing.txt")), -1);
}

void TestSortFilter::findByPrefixWrapsAround()
{
    TempTree tree;
    for (const QString &name : { "apple.txt", "apricot.txt", "banana.txt" })
        tree.writeFile(name);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 3);

    QCOMPARE(proxy.findByPrefix(QStringLiteral("ap"), 0), 0);
    QCOMPARE(proxy.findByPrefix(QStringLiteral("apr"), 0), 1);
    QCOMPARE(proxy.findByPrefix(QStringLiteral("b"), 0), 2);

    // Searching from past a match must wrap rather than give up.
    QCOMPARE(proxy.findByPrefix(QStringLiteral("apple"), 2), 0);
    QCOMPARE(proxy.findByPrefix(QStringLiteral("zzz"), 0), -1);
    QCOMPARE(proxy.findByPrefix(QString(), 0), -1);
}

void TestSortFilter::valueAtReadsNamedRoles()
{
    TempTree tree;
    tree.writeFile("thing.txt", 42);

    DirectoryModel model;
    FileSortFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setPath(tree.path());
    QTRY_COMPARE(proxy.rowCount(), 1);

    QCOMPARE(proxy.valueAt(0, QStringLiteral("name")).toString(), QStringLiteral("thing.txt"));
    QCOMPARE(proxy.valueAt(0, QStringLiteral("size")).toLongLong(), 42);
    QCOMPARE(proxy.valueAt(0, QStringLiteral("isDir")).toBool(), false);
    QCOMPARE(proxy.valueAt(0, QStringLiteral("filePath")).toString(), tree.filePath("thing.txt"));

    // Unknown roles and out-of-range rows return an invalid variant rather
    // than something that silently reads as an empty string.
    QVERIFY(!proxy.valueAt(0, QStringLiteral("nosuchrole")).isValid());
    QVERIFY(!proxy.valueAt(99, QStringLiteral("name")).isValid());
}

QTEST_GUILESS_MAIN(TestSortFilter)
#include "tst_sortfilter.moc"
