#include "PickerFilter.h"

#include <QSignalSpy>
#include <QTest>

// The file chooser's type filter: what an app's "Images" or "PDF document"
// lets the person pick. Rows it rules out are dimmed and cannot be chosen,
// so a wrong answer here is a file the app cannot open, or one it can that
// the person is not allowed to pick.
class TestPicker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void noRulesMatchesEverything();
    void globsIgnoreCase();
    void mimeTypesFollowSubclasses();
    void mimeFamiliesByWildcard();
    void foldersAlwaysMatch();
    void rulesChangeBumpsTheRevision();
};

static QVariantMap rule(uint type, const QString &pattern)
{
    return { { QStringLiteral("type"), type }, { QStringLiteral("pattern"), pattern } };
}

void TestPicker::noRulesMatchesEverything()
{
    PickerFilter filter;
    QVERIFY(filter.matches(QStringLiteral("anything.bin"), QStringLiteral("application/octet-stream"), false));
}

void TestPicker::globsIgnoreCase()
{
    PickerFilter filter;
    filter.setRules({ rule(0, QStringLiteral("*.jpg")), rule(0, QStringLiteral("*.png")) });
    QVERIFY(filter.matches(QStringLiteral("beach.jpg"), {}, false));
    QVERIFY(filter.matches(QStringLiteral("IMG_0001.JPG"), {}, false)); // straight off a camera card
    QVERIFY(filter.matches(QStringLiteral("map.png"), {}, false));
    QVERIFY(!filter.matches(QStringLiteral("notes.txt"), {}, false));
    QVERIFY(!filter.matches(QStringLiteral("beach.jpg.txt"), {}, false));
}

void TestPicker::mimeTypesFollowSubclasses()
{
    PickerFilter filter;
    filter.setRules({ rule(1, QStringLiteral("text/plain")) });
    QVERIFY(filter.matches(QStringLiteral("notes.txt"), QStringLiteral("text/plain"), false));
    // A C source file is a kind of plain text.
    QVERIFY(filter.matches(QStringLiteral("main.c"), QStringLiteral("text/x-csrc"), false));
    QVERIFY(!filter.matches(QStringLiteral("beach.jpg"), QStringLiteral("image/jpeg"), false));
    // No detected type: only a glob could have said yes.
    QVERIFY(!filter.matches(QStringLiteral("mystery"), QString(), false));
}

void TestPicker::mimeFamiliesByWildcard()
{
    PickerFilter filter;
    filter.setRules({ rule(1, QStringLiteral("image/*")) });
    QVERIFY(filter.matches(QStringLiteral("a.jpg"), QStringLiteral("image/jpeg"), false));
    QVERIFY(filter.matches(QStringLiteral("a.cr3"), QStringLiteral("image/x-canon-cr3"), false));
    QVERIFY(!filter.matches(QStringLiteral("a.mp4"), QStringLiteral("video/mp4"), false));
    // "imagery/x" is not the image family.
    QVERIFY(!filter.matches(QStringLiteral("a.x"), QStringLiteral("imagery/x"), false));
}

void TestPicker::foldersAlwaysMatch()
{
    PickerFilter filter;
    filter.setRules({ rule(0, QStringLiteral("*.pdf")) });
    // Or there would be no way to walk to the file.
    QVERIFY(filter.matches(QStringLiteral("Documents"), QStringLiteral("inode/directory"), true));
}

void TestPicker::rulesChangeBumpsTheRevision()
{
    PickerFilter filter;
    QSignalSpy spy(&filter, &PickerFilter::rulesChanged);
    const int before = filter.revision();
    filter.setRules({ rule(0, QStringLiteral("*.pdf")) });
    QCOMPARE(spy.count(), 1);
    QVERIFY(filter.revision() > before);
    filter.setRules({ rule(0, QStringLiteral("*.pdf")) }); // same rules: nothing to redraw
    QCOMPARE(spy.count(), 1);
    filter.setRules({});
    QVERIFY(filter.matches(QStringLiteral("notes.txt"), QString(), false));
}

QTEST_MAIN(TestPicker)
#include "tst_picker.moc"
