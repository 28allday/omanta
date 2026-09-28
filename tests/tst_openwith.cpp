#include "Platform.h"
#include "TestFixture.h"

#include <QTest>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// Context-menu "Open With": the synchronous query behind the right-click
// submenu. FileProperties already answers this asynchronously for its dialog;
// the menu needs the same answer synchronously when it opens.
class TestOpenWith : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsApplicationsForAFile();
    void offersNothingForAFolder();
};

void TestOpenWith::listsApplicationsForAFile()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("readme.txt"), 10);

    Platform platform;
    const QVariantList applications = platform.applicationsFor(path);
    if (applications.isEmpty())
        QSKIP("no application on this system is registered for text/plain");

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
    QVERIFY(defaults <= 1);
}

void TestOpenWith::offersNothingForAFolder()
{
    TempTree tree;
    const QString folder = tree.makeDir(QStringLiteral("folder"));

    Platform platform;
    // "Open With" on a folder would offer to hand it to a text editor.
    QVERIFY(platform.applicationsFor(folder).isEmpty());
    QVERIFY(platform.applicationsFor(QString()).isEmpty());
}

QTEST_GUILESS_MAIN(TestOpenWith)
#include "tst_openwith.moc"
