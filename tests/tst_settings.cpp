#include "Settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// The preferences store: plain watched file, Nautilus-default values,
// validation on read. Each test points OMANTA_SETTINGS_FILE at its own
// scratch file — the suite must never read or write the real config.
class TestSettings : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void defaultsAreNautilus();
    void settersPersistAcrossInstances();
    void invalidValuesFallBackToDefaults();
    void externalEditsReloadLive();
    void commentsAndBlanksAreIgnored();
    void listColumnDefaultsAreNautilus();
    void listColumnListsValidateAndRepair();
    void listVisibleFollowsColumnOrder();
    void iconCaptionsAlwaysThreeSlots();
    void backgroundOpacityValidates();

private:
    QTemporaryDir m_dir;
    QString m_file;

    void write(const QByteArray &content)
    {
        QFile file(m_file);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }
};

void TestSettings::init()
{
    QVERIFY(m_dir.isValid());
    m_file = m_dir.filePath(QStringLiteral("settings-%1").arg(QTest::currentTestFunction()));
    qputenv("OMANTA_SETTINGS_FILE", m_file.toUtf8());
}

void TestSettings::defaultsAreNautilus()
{
    Settings settings;

    // GNOME schema defaults — the parity contract for a fresh Omarchy.
    QCOMPARE(settings.sortFoldersFirst(), false);
    QCOMPARE(settings.clickPolicy(), QStringLiteral("double"));
    QCOMPARE(settings.useTreeView(), false);
    QCOMPARE(settings.showCreateLink(), false);
    QCOMPARE(settings.showDeletePermanently(), false);
    QCOMPARE(settings.searchInSubfolders(), QStringLiteral("local-only"));
    QCOMPARE(settings.showThumbnails(), QStringLiteral("local-only"));
    QCOMPARE(settings.showDirectoryItemCounts(), QStringLiteral("local-only"));
    QCOMPARE(settings.dateTimeFormat(), QStringLiteral("simple"));
    QCOMPARE(settings.defaultViewMode(), QStringLiteral("icon"));
}

void TestSettings::settersPersistAcrossInstances()
{
    {
        Settings settings;
        settings.setSortFoldersFirst(true);
        settings.setClickPolicy(QStringLiteral("single"));
        settings.setDateTimeFormat(QStringLiteral("detailed"));
    }

    Settings reread;
    QCOMPARE(reread.sortFoldersFirst(), true);
    QCOMPARE(reread.clickPolicy(), QStringLiteral("single"));
    QCOMPARE(reread.dateTimeFormat(), QStringLiteral("detailed"));
    // Untouched keys still answer their defaults.
    QCOMPARE(reread.showThumbnails(), QStringLiteral("local-only"));
}

void TestSettings::invalidValuesFallBackToDefaults()
{
    write("sortFoldersFirst=yes please\n"
          "clickPolicy=triple\n"
          "showThumbnails=sometimes\n"
          "showDirectoryItemCounts=weekly\n"
          "dateTimeFormat=cuneiform\n");

    Settings settings;
    QCOMPARE(settings.sortFoldersFirst(), false);
    QCOMPARE(settings.clickPolicy(), QStringLiteral("double"));
    QCOMPARE(settings.showThumbnails(), QStringLiteral("local-only"));
    QCOMPARE(settings.showDirectoryItemCounts(), QStringLiteral("local-only"));
    QCOMPARE(settings.dateTimeFormat(), QStringLiteral("simple"));
}

void TestSettings::externalEditsReloadLive()
{
    write("sortFoldersFirst=false\n");
    Settings settings;
    QCOMPARE(settings.sortFoldersFirst(), false);

    QSignalSpy spy(&settings, &Settings::changed);
    write("sortFoldersFirst=true\n");

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    QCOMPARE(settings.sortFoldersFirst(), true);
}

void TestSettings::commentsAndBlanksAreIgnored()
{
    write("# omanta settings\n"
          "\n"
          "  clickPolicy = single  \n"
          "not a key value line\n");

    Settings settings;
    QCOMPARE(settings.clickPolicy(), QStringLiteral("single"));
}

void TestSettings::listColumnDefaultsAreNautilus()
{
    Settings settings;

    // GNOME's default-visible-columns; the order is the full canonical set.
    QCOMPARE(settings.listVisibleColumns(),
             (QStringList{ "name", "size", "modified" }));
    QCOMPARE(settings.listColumnOrder(), Settings::allListColumns());
    QCOMPARE(settings.listColumnOrder().first(), QStringLiteral("name"));
}

void TestSettings::listColumnListsValidateAndRepair()
{
    write("listVisibleColumns=size,bogus,owner\n"
          "listColumnOrder=modified,paisley,type\n");

    Settings settings;

    // Unknown ids dropped, name reinstated first, missing ids appended in
    // canonical order — the file's modified-before-type wish is kept.
    const QStringList order = settings.listColumnOrder();
    QCOMPARE(order.first(), QStringLiteral("name"));
    QCOMPARE(order.size(), Settings::allListColumns().size());
    QVERIFY(!order.contains(QStringLiteral("paisley")));
    QVERIFY(order.indexOf("modified") < order.indexOf("type"));

    // Visible: name forced in, the file's valid picks presented in column
    // order (size precedes owner canonically after the repair).
    QCOMPARE(settings.listVisibleColumns(),
             (QStringList{ "name", "size", "owner" }));

    // An empty visible list cannot strip the view bare — defaults return.
    write("listVisibleColumns=\n");
    Settings empty;
    QCOMPARE(empty.listVisibleColumns(),
             (QStringList{ "name", "size", "modified" }));
}

void TestSettings::listVisibleFollowsColumnOrder()
{
    write("listColumnOrder=name,modified,size,type,owner,group,permissions,created,accessed\n"
          "listVisibleColumns=size,modified\n");

    Settings settings;
    QCOMPARE(settings.listVisibleColumns(),
             (QStringList{ "name", "modified", "size" }));
}

void TestSettings::iconCaptionsAlwaysThreeSlots()
{
    // GNOME's schema default: no captions at all.
    Settings fresh;
    QCOMPARE(fresh.iconCaptions(), (QStringList{ "none", "none", "none" }));

    // An unknown id becomes "none" IN PLACE — a typo in slot one must not
    // promote slot two; short lists pad, long lists truncate, and "name" is
    // not a caption (it is the label itself).
    write("iconCaptions=bogus,size,modified,owner\n");
    Settings typo;
    QCOMPARE(typo.iconCaptions(), (QStringList{ "none", "size", "modified" }));

    write("iconCaptions=size\n");
    Settings padded;
    QCOMPARE(padded.iconCaptions(), (QStringList{ "size", "none", "none" }));

    write("iconCaptions=name,size,type\n");
    Settings named;
    QCOMPARE(named.iconCaptions(), (QStringList{ "none", "size", "type" }));
}

void TestSettings::backgroundOpacityValidates()
{
    // Solid by default; the terminal-style translucency is opt-in.
    Settings fresh;
    QCOMPARE(fresh.backgroundOpacity(), 1.0);

    fresh.setBackgroundOpacity(0.92);
    Settings persisted;
    QCOMPARE(persisted.backgroundOpacity(), 0.92);

    // Below the readability floor, above 1, or not a number → default.
    write("backgroundOpacity=0.3\n");
    Settings low;
    QCOMPARE(low.backgroundOpacity(), 1.0);

    write("backgroundOpacity=1.5\n");
    Settings high;
    QCOMPARE(high.backgroundOpacity(), 1.0);

    write("backgroundOpacity=opaque\n");
    Settings garbage;
    QCOMPARE(garbage.backgroundOpacity(), 1.0);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
