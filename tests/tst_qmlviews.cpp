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
#include <QTest>

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
static QQuickItem *findFileRow(QQuickItem *item, const QString &path)
{
    if (item->property("filePath").toString() == path)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (auto *found = findFileRow(child, path))
            return found;
    }
    return nullptr;
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
