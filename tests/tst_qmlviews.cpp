#include "Application.h"
#include "IconImageProvider.h"
#include "Platform.h"
#include "ServerStore.h"
#include "StarredStore.h"
#include "SystemTheme.h"
#include "ThumbnailProvider.h"
#include "TestFixture.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>

class TestQmlViews : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void selectionAndVirtualDelegates();
};

static QVariant invoke(QObject *object, const char *method)
{
    QVariant result;
    QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result));
    return result.value<QJSValue>().toVariant();
}

// The visual tree includes instantiated view delegates, unlike QObject's
// ownership tree where a Loader or the view can keep them elsewhere.
static QQuickItem *findItem(QQuickItem *item, const char *property, const QVariant &value)
{
    if (item->property(property) == value)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (auto *found = findItem(child, property, value))
            return found;
    }
    return nullptr;
}

static QQuickItem *findFileRow(QQuickItem *item, const QString &path)
{
    return findItem(item, "filePath", path);
}

static void checkListIconSizing(QQuickWindow *window, QQuickItem *tab,
                                const TempTree &tree, const QStringList &names)
{
    QVERIFY(window);
    window->requestActivate();
    QTRY_VERIFY(window->isActive());
    tab->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("viewMode").toString(), QStringLiteral("list"));
    const QString path = tree.filePath(names.first());
    QTRY_VERIFY(findItem(tab, "previewPath", path));
    auto *preview = findItem(tab, "previewPath", path);
    QCOMPARE(preview->width(), 18);
    QCOMPARE(findFileRow(tab, path)->height(), 30);

    QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 24);
    QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 32);
    QCOMPARE(preview->property("sourceSize").toSize(), QSize(32, 32));
    QTRY_COMPARE(findFileRow(tab, path)->height(), 44);
    for (int i = 0; i < 8; ++i)
        QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 64);

    QList<QQuickItem *> rows;
    for (const QString &name : names) {
        auto *row = findFileRow(tab, tree.filePath(name));
        QVERIFY(row);
        rows << row;
    }
    std::sort(rows.begin(), rows.end(), [](auto *a, auto *b) {
        return a->property("index").toInt() < b->property("index").toInt();
    });
    for (int i = 1; i < rows.size(); ++i)
        QTRY_COMPARE(rows[i]->y() - rows[i - 1]->y(), rows[i - 1]->height());

    // Clicking an enlarged row and drawing a band from below the list must
    // still operate on precisely the files under the pointer.
    const QPoint target = rows[2]->mapToScene(QPointF(100, rows[2]->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target);
    QTRY_COMPARE(invoke(tab, "selectedPaths").toStringList(),
                 QStringList{rows[2]->property("filePath").toString()});
    const QPoint below = rows.last()->mapToScene(QPointF(200, rows.last()->height() + 20)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, below);
    QTest::mouseMove(window, target, 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, target);
    const QStringList lastTwo{rows[2]->property("filePath").toString(),
                              rows[3]->property("filePath").toString()};
    QTRY_COMPARE(invoke(tab, "selectedPaths").toStringList(), lastTwo);

    const QString screenshot = qEnvironmentVariable("OMANTA_TEST_SCREENSHOT");
    if (!screenshot.isEmpty())
        QVERIFY(window->grabWindow().save(screenshot));

    auto *options = findItem(window->contentItem(), "tip", QStringLiteral("View options"));
    QVERIFY(options);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        options->mapToScene(QPointF(options->width() / 2, options->height() / 2)).toPoint());
    QTRY_VERIFY(findItem(window->contentItem(), "tip", QStringLiteral("Zoom out (Ctrl+-)")));
    auto *smaller = findItem(window->contentItem(), "tip", QStringLiteral("Zoom out (Ctrl+-)"));
    QTRY_VERIFY(smaller->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        smaller->mapToScene(QPointF(smaller->width() / 2, smaller->height() / 2)).toPoint());
    QTRY_COMPARE(preview->width(), 48);
    QTest::keyClick(window, Qt::Key_Escape);

    // List and grid sizes survive switching independently.
    QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 64);
    QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 80);
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 48);
    for (int i = 0; i < 8; ++i)
        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
    QTRY_COMPARE(findItem(tab, "previewPath", path)->width(), 16);
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(findItem(tab, "previewPath", path)->width(), 18);
    QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 80);
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 64);
}

void TestQmlViews::selectionAndVirtualDelegates()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"OMANTA_SETTINGS_FILE", "OMANTA_STARRED_FILE",
                           "OMANTA_SERVERS_FILE", "OMANTA_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("OMANTA_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    const QStringList names{"selected.txt", "constructor", "toString", "__proto__"};
    for (const QString &name : names)
        tree.writeFile(name);
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QObject *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = child;
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
    for (const QString &name : names) {
        QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, name)));
        QCOMPARE(tab->property("selectionCount").toInt(), 1);
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath(name)});
        QCOMPARE(invoke(tab, "selectedItems").toList().size(), 1);
        QVERIFY(QMetaObject::invokeMethod(tab, "toggleSelection", Q_ARG(QVariant, name)));
        QCOMPARE(tab->property("selectionCount").toInt(), 0);
        QVERIFY(invoke(tab, "selectedPaths").toStringList().isEmpty());
    }
    QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
    QCOMPARE(tab->property("selectionCount").toInt(), 4);
    QCOMPARE(invoke(tab, "selectedPaths").toStringList().size(), 4);
    QVERIFY(QMetaObject::invokeMethod(tab, "clearSelection"));
    QVERIFY(invoke(tab, "selectedPaths").toStringList().isEmpty());

    checkListIconSizing(qobject_cast<QQuickWindow *>(window), tab, tree, names);

    const QString selected = tree.filePath("selected.txt");
    auto *stars = engine.singletonInstance<StarredStore *>("Omanta", "StarredStore");
    auto *servers = engine.singletonInstance<ServerStore *>("Omanta", "ServerStore");
    QVERIFY(stars);
    QVERIFY(servers);
    stars->star({selected});
    const QString server = QStringLiteral("sftp://example.invalid/share");
    servers->add(server);
    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        tab->setProperty("path", tree.path());
        tab->setProperty("searchQuery", "selected");
        QTRY_VERIFY_WITH_TIMEOUT(!tab->property("searching").toBool()
            && window->property("visibleCount").toInt() == 1, 10000);
        QTRY_VERIFY(findFileRow(tab, selected));
        tab->setProperty("searchQuery", "");
        tab->setProperty("path", "starred:///");
        QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
        QTRY_VERIFY(findFileRow(tab, selected));
        tab->setProperty("path", "network:///");
        QTRY_VERIFY(window->property("visibleCount").toInt() >= 1);
        QTRY_VERIFY(findFileRow(tab, server));
    }
}

QTEST_MAIN(TestQmlViews)
#include "tst_qmlviews.moc"
