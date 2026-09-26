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
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QStyleHints>
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

static void checkDragPreviews(QQuickWindow *window, QQuickItem *tab,
                              const TempTree &tree, const QStringList &names,
                              const QColor &thumbnailColor = {})
{
    const QString path = tree.filePath(names.first());
    const int pressDelay = QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;
    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon")}) {
        tab->setProperty("viewMode", mode);
        if (thumbnailColor.isValid()) {
            // Match the drag's thumbnail request so it can reuse a ready
            // image, avoiding dependence on the decoder's scheduling speed.
            QVERIFY(QMetaObject::invokeMethod(tab, "setZoom", Q_ARG(QVariant, 36)));
        }
        QTRY_VERIFY(findFileRow(tab, path));
        auto *row = findFileRow(tab, path);
        if (thumbnailColor.isValid()) {
            QTRY_VERIFY(findItem(row, "previewPath", path));
            QTRY_COMPARE(findItem(row, "previewPath", path)->property("status").toInt(), 1);
        }
        QTRY_VERIFY(findItem(row, "ready", false));
        auto *drag = findItem(row, "ready", false);
        for (bool multiple : {false, true}) {
            QVERIFY(QMetaObject::invokeMethod(tab, multiple ? "selectAll" : "clearSelection"));
            // In grid view the centre is within the icon/label hit area;
            // in list view it lands in the row, away from its expander.
            const QPoint point = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point, pressDelay);
            QTRY_VERIFY(drag->property("ready").toBool());
            QCOMPARE(drag->property("itemCount").toInt(), multiple ? names.size() : 1);
            const auto data = drag->property("mimeData").value<QJSValue>().toVariant().toMap();
            Platform platform;
            const QStringList paths = invoke(tab, "selectedPaths").toStringList();
            QCOMPARE(data.value("text/uri-list").toString(), platform.uriList(paths));
            QCOMPARE(paths.size(), multiple ? names.size() : 1);

            auto *grab = qobject_cast<QQuickItemGrabResult *>(
                drag->property("grabResult").value<QObject *>());
            QVERIFY(grab);
            const QImage image = grab->image();
            QVERIFY(!image.isNull());
            const qreal scale = window->devicePixelRatio();
            QVERIFY(image.width() <= 300 * scale);
            QCOMPARE(image.height(), qRound(56 * scale));
            // A grab must contain the card despite its transparent parent.
            QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).alpha() > 0);
            if (thumbnailColor.isValid())
                QCOMPARE(image.pixelColor(qRound(28 * scale), qRound(28 * scale)), thumbnailColor);
            const QString screenshot = qEnvironmentVariable("OMANTA_TEST_DRAG_SCREENSHOT");
            if (!screenshot.isEmpty())
                QVERIFY(image.save(screenshot + "-" + mode + (multiple ? "-multi.png" : "-single.png")));

            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(!drag->property("ready").toBool());
            QVERIFY(drag->property("previewUrl").toUrl().isEmpty());
        }

        // Releasing before the asynchronous grab completes must not leave a
        // stale preview ready for the next press (or start a late drag).
        const QPoint point = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point, pressDelay);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(100);
        QVERIFY(!drag->property("ready").toBool());
    }
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
    const QStringList names{QStringLiteral("selected-") + QString(160, QLatin1Char('a')) + ".txt",
                            "constructor", "toString", "__proto__"};
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
    if (QTest::currentTestFailed())
        return;
    checkDragPreviews(qobject_cast<QQuickWindow *>(window), tab, tree, names);
    if (QTest::currentTestFailed())
        return;

    TempTree photos;
    const QColor thumbnailColor("#31c983");
    QImage photo(80, 40, QImage::Format_RGB32);
    photo.fill(thumbnailColor);
    QVERIFY(photo.save(photos.filePath("photo.png")));
    photos.writeFile("other.txt");
    tab->setProperty("path", photos.path());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    checkDragPreviews(qobject_cast<QQuickWindow *>(window), tab, photos,
                      {"photo.png", "other.txt"}, thumbnailColor);
    if (QTest::currentTestFailed())
        return;

    const QString selected = tree.filePath(names.first());
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
