#include "DirectoryModel.h"
#include "DirectoryTreeModel.h"
#include "FileSortFilterModel.h"
#include "TestFixture.h"

#include <QFile>
#include <QTest>

// Expandable folders: a flat projection of a tree. What matters here is that
// the projection stays truthful while every level changes underneath it —
// each expanded folder has its own live monitor, and the flat rows have to
// follow all of them at once.
class TestTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void flattensRootWhenNothingIsExpanded();
    void expandInsertsChildrenUnderTheParent();
    void collapseRemovesTheWholeSubtree();
    void nestedChangesAppearLive();
    void sortsWithinEachLevel();
    void hiddenToggleWorksAtEveryDepth();
    void deletingAnExpandedFolderPrunesItsNode();
    void navigationCollapsesEverything();
    void typeAheadSeesNestedRows();
    void valueAtReadsTreeRoles();

private:
    // Name-role values in flat order — what selection and the view see.
    static QStringList visible(const DirectoryTreeModel &tree)
    {
        QStringList names;
        for (int row = 0; row < tree.count(); ++row)
            names << tree.valueAt(row, QStringLiteral("name")).toString();
        return names;
    }
};

void TestTree::flattensRootWhenNothingIsExpanded()
{
    TempTree fixture;
    fixture.writeFile("beta.txt");
    fixture.writeFile("alpha.txt");
    fixture.makeDir("zebra");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());

    QTRY_COMPARE(tree.count(), 3);
    // Name order with folders-first defaulting on — the proxy's rules, one
    // level deep, names plain at depth 0.
    QCOMPARE(visible(tree), (QStringList{ "zebra", "alpha.txt", "beta.txt" }));
    for (int row = 0; row < tree.count(); ++row) {
        QCOMPARE(tree.valueAt(row, QStringLiteral("depth")).toInt(), 0);
        QCOMPARE(tree.valueAt(row, QStringLiteral("expanded")).toBool(), false);
    }
}

void TestTree::expandInsertsChildrenUnderTheParent()
{
    TempTree fixture;
    fixture.writeFile("apple.txt");
    fixture.writeFile("zebra/inner_b.txt");
    fixture.writeFile("zebra/inner_a.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 2);

    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 4);

    QCOMPARE(visible(tree), (QStringList{
        "zebra", "zebra/inner_a.txt", "zebra/inner_b.txt", "apple.txt" }));
    QCOMPARE(tree.valueAt(0, QStringLiteral("expanded")).toBool(), true);
    QCOMPARE(tree.valueAt(1, QStringLiteral("depth")).toInt(), 1);
    // Display names stay the basename — the path lives in the name role only.
    QCOMPARE(tree.valueAt(1, QStringLiteral("displayName")).toString(),
             QStringLiteral("inner_a.txt"));
    QCOMPARE(tree.valueAt(1, QStringLiteral("filePath")).toString(),
             fixture.filePath(QStringLiteral("zebra/inner_a.txt")));
}

void TestTree::collapseRemovesTheWholeSubtree()
{
    TempTree fixture;
    fixture.writeFile("zebra/deeper/leaf.txt");
    fixture.writeFile("zebra/file.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 1);

    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 3);
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra/deeper")));
    QTRY_COMPARE(tree.count(), 4);
    QCOMPARE(tree.valueAt(2, QStringLiteral("depth")).toInt(), 2);

    // Collapsing the ancestor takes the grandchild's rows and node with it.
    tree.collapse(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 1);
    QCOMPARE(tree.valueAt(0, QStringLiteral("expanded")).toBool(), false);

    // Re-expanding starts fresh: the grandchild is back to collapsed.
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 3);
    QCOMPARE(tree.valueAt(tree.proxyRowForName(QStringLiteral("zebra/deeper")),
                          QStringLiteral("expanded")).toBool(), false);
}

void TestTree::nestedChangesAppearLive()
{
    TempTree fixture;
    fixture.writeFile("zebra/one.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 1);
    tree.expand(0);
    QTRY_COMPARE(tree.count(), 2);

    // The expanded folder has its own monitor: a file appearing inside it is
    // an insert on the flat rows, no reload, no reset.
    fixture.writeFile("zebra/two.txt");
    QTRY_COMPARE(tree.count(), 3);
    QVERIFY(visible(tree).contains(QStringLiteral("zebra/two.txt")));

    fixture.remove("zebra/one.txt");
    QTRY_COMPARE(tree.count(), 2);
    QVERIFY(!visible(tree).contains(QStringLiteral("zebra/one.txt")));
}

void TestTree::sortsWithinEachLevel()
{
    TempTree fixture;
    fixture.writeFile("outer_b.txt");
    fixture.writeFile("zebra/inner_b.txt", 10);
    fixture.writeFile("zebra/inner_a.txt", 500);

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 2);
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 4);

    // Children order by the same key as everything else, inside their parent.
    tree.setSortKey(FileSortFilterModel::BySize);
    QTRY_COMPARE(visible(tree), (QStringList{
        "zebra", "zebra/inner_b.txt", "zebra/inner_a.txt", "outer_b.txt" }));

    tree.setSortDescending(true);
    QTRY_COMPARE(visible(tree), (QStringList{
        "zebra", "zebra/inner_a.txt", "zebra/inner_b.txt", "outer_b.txt" }));
}

void TestTree::hiddenToggleWorksAtEveryDepth()
{
    TempTree fixture;
    fixture.writeFile("zebra/.secret");
    fixture.writeFile("zebra/plain.txt");
    fixture.writeFile(".shadows/inside.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 1); // .shadows is filtered, zebra remains
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 2); // .secret filtered at depth 1 too

    tree.setShowHidden(true);
    QTRY_COMPARE(tree.count(), 4);
    QVERIFY(visible(tree).contains(QStringLiteral("zebra/.secret")));

    // Expand the hidden folder, then hide hidden files again: the folder's
    // row goes, and its expanded children must not linger behind it.
    tree.expand(tree.proxyRowForName(QStringLiteral(".shadows")));
    QTRY_COMPARE(tree.count(), 5);
    tree.setShowHidden(false);
    QTRY_COMPARE(tree.count(), 2);
    QVERIFY(!visible(tree).contains(QStringLiteral(".shadows/inside.txt")));
}

void TestTree::deletingAnExpandedFolderPrunesItsNode()
{
    TempTree fixture;
    fixture.writeFile("zebra/inner.txt");
    fixture.writeFile("keep.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 2);
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 3);

    // Deleting the folder on disk removes its row and its children's — and
    // the node behind them, or its monitor would live on invisibly.
    QVERIFY(QFile::remove(fixture.filePath(QStringLiteral("zebra/inner.txt"))));
    QVERIFY(QDir(fixture.filePath(QStringLiteral("zebra"))).removeRecursively());
    QTRY_COMPARE(tree.count(), 1);
    QCOMPARE(visible(tree), (QStringList{ "keep.txt" }));
}

void TestTree::navigationCollapsesEverything()
{
    TempTree fixture;
    fixture.writeFile("zebra/inner.txt");
    fixture.writeFile("zebra/deeper/leaf.txt");
    TempTree other;
    other.writeFile("elsewhere.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 1);
    tree.expand(0);
    QTRY_COMPARE(tree.count(), 3);

    // A new location is a new tree: the old expansions belong to a listing
    // that no longer exists.
    root.setPath(other.path());
    QTRY_COMPARE(visible(tree), (QStringList{ "elsewhere.txt" }));
    QCOMPARE(tree.valueAt(0, QStringLiteral("depth")).toInt(), 0);
}

void TestTree::typeAheadSeesNestedRows()
{
    TempTree fixture;
    fixture.writeFile("apple.txt");
    fixture.writeFile("zebra/quince.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 2);
    tree.expand(tree.proxyRowForName(QStringLiteral("zebra")));
    QTRY_COMPARE(tree.count(), 3);

    // Type-ahead matches the display name (the basename), whatever the depth.
    const int row = tree.findByPrefix(QStringLiteral("qu"), 0);
    QCOMPARE(tree.valueAt(row, QStringLiteral("name")).toString(),
             QStringLiteral("zebra/quince.txt"));
}

void TestTree::valueAtReadsTreeRoles()
{
    TempTree fixture;
    fixture.writeFile("zebra/inner.txt");

    DirectoryModel root;
    DirectoryTreeModel tree;
    tree.setRootModel(&root);
    root.setPath(fixture.path());
    QTRY_COMPARE(tree.count(), 1);
    tree.expand(0);
    QTRY_COMPARE(tree.count(), 2);

    QCOMPARE(tree.proxyRowForName(QStringLiteral("zebra/inner.txt")), 1);
    QCOMPARE(tree.valueAt(1, QStringLiteral("isDir")).toBool(), false);
    QCOMPARE(tree.valueAt(0, QStringLiteral("isDir")).toBool(), true);
    QCOMPARE(tree.valueAt(0, QStringLiteral("expanded")).toBool(), true);
    QCOMPARE(tree.valueAt(1, QStringLiteral("expanded")).toBool(), false);
    QVERIFY(tree.valueAt(99, QStringLiteral("name")).isNull());
}

QTEST_GUILESS_MAIN(TestTree)
#include "tst_tree.moc"
