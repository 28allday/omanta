#include "Application.h"
#include "Clipboard.h"
#include "DirectoryModel.h"
#include "FileOperations.h"
#include "IconImageProvider.h"
#include "Mounter.h"
#include "Platform.h"
#include "ServerStore.h"
#include "Settings.h"
#include "StarredStore.h"
#include "SystemTheme.h"
#include "ThumbnailProvider.h"
#include "TestFixture.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTest>

#include <algorithm>
#include <functional>

// Stands in for Sushi 50 on the session bus under a private name, so Space
// can be driven end to end without a preview window ever opening. Like the
// real one it takes (ssbs) and announces only Visible=false.
class FakePreviewer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.NautilusPreviewer2")
    Q_PROPERTY(bool Visible READ visible)

public:
    struct Call { QString uri; bool toggle; };
    QList<Call> calls;
    bool shown = false;

    bool visible() const { return shown; }

public Q_SLOTS:
    void ShowFile(const QString &uri, const QString &, bool closeIfShown, const QString &)
    {
        calls.append({uri, closeIfShown});
        setShown(!(closeIfShown && shown));
    }
    void Close() { setShown(false); }

public:
    // An arrow key pressed inside the preview window. Sushi 50 sends it as
    // (u) although it introspects as (q); both must work.
    template <typename Direction>
    void pressInPreview(Direction direction)
    {
        QDBusMessage event = QDBusMessage::createSignal(
            QStringLiteral("/org/gnome/NautilusPreviewer"),
            QStringLiteral("org.gnome.NautilusPreviewer2"), QStringLiteral("SelectionEvent"));
        event << QVariant::fromValue(direction);
        QDBusConnection::sessionBus().send(event);
    }

private:
    void setShown(bool value)
    {
        if (shown == value)
            return;
        shown = value;
        if (value)
            return; // Sushi 50 never announces an open
        QDBusMessage changed = QDBusMessage::createSignal(
            QStringLiteral("/org/gnome/NautilusPreviewer"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        changed << QStringLiteral("org.gnome.NautilusPreviewer2")
                << QVariantMap{{QStringLiteral("Visible"), value}} << QStringList();
        QDBusConnection::sessionBus().send(changed);
    }
};

class TestQmlViews : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void selectionAndVirtualDelegates();
    void emptyTrashRefreshesOpenViews();
    void pasteKeepsCopiedFilesOnClipboard();
    void thumbnailsFollowInPlaceEdits();
    void dragPreviewSurvivesItsOwner();
    void spacePreviewsInSushi();

private:
    QTemporaryDir m_cache;
};

void TestQmlViews::initTestCase()
{
    // Thumbnails are generated here, and must never land in the real
    // ~/.cache/thumbnails shared with every other application.
    QVERIFY(m_cache.isValid());
    qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    // Every window offers the Omarchy Toggle-menu row on first launch; these
    // suites must never edit the real desktop's menu or bindings.
    qputenv("OMANTA_SWITCH", "/nonexistent/omanta-switch");
    // Space must never reach the real Sushi from a test run.
    qputenv("OMANTA_PREVIEWER_SERVICE",
            QByteArray("org.omarchy.omanta.TestPreviewer") + QByteArray::number(QCoreApplication::applicationPid()));
}

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

    // Sizes are remembered: saved to the settings file, and a new tab (as a
    // new window or a restart would) opens at them rather than the default.
    const auto saved = [] {
        QFile settings(qEnvironmentVariable("OMANTA_SETTINGS_FILE"));
        return settings.open(QIODevice::ReadOnly) ? settings.readAll() : QByteArray();
    };
    QTRY_VERIFY(saved().contains("iconZoom=80"));
    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier);
    QTRY_VERIFY(window->property("currentTab").value<QObject *>() != tab);
    auto *second = window->property("currentTab").value<QObject *>();
    QTRY_COMPARE(second->property("zoom").toInt(), 80);
    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
    QTRY_VERIFY(window->property("currentTab").value<QObject *>() == tab);

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


static void checkLiveSelection(QQuickWindow *window, QQuickItem *tab, Settings *settings)
{
    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon"), QStringLiteral("tree")}) {
        TempTree tree;
        tree.writeFile("a");
        tree.writeFile("__proto__");
        tree.writeFile("z");
        settings->setUseTreeView(mode == "tree");
        tab->setProperty("viewMode", mode == "icon" ? "icon" : "list");
        tab->setProperty("path", tree.path());
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
        QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, "a")));
        tab->setProperty("currentIndex", tab->property("anchorIndex"));
        const auto currentName = [&] {
            auto *files = tab->property("files").value<QObject *>();
            QVariant value;
            QMetaObject::invokeMethod(files, "valueAt", Q_RETURN_ARG(QVariant, value),
                Q_ARG(int, tab->property("currentIndex").toInt()), Q_ARG(QString, QString("name")));
            return value.toString();
        };
        QCOMPARE(currentName(), QStringLiteral("a"));
        tab->setProperty("sortDescending", true);
        QTRY_COMPARE(currentName(), QStringLiteral("a"));
        tree.writeFile("b");
        QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
        QCOMPARE(currentName(), QStringLiteral("a"));
        QVERIFY(QFile::remove(tree.filePath("z")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
        QCOMPARE(currentName(), QStringLiteral("a"));
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath("a")});
        QVERIFY(QMetaObject::invokeMethod(tab, "toggleSelection", Q_ARG(QVariant, "__proto__")));
        QCOMPARE(tab->property("selectionCount").toInt(), 2);
        QVERIFY(QFile::remove(tree.filePath("a")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 1);
        QCOMPARE(tab->property("currentIndex").toInt(), -1);
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath("__proto__")});
        QVERIFY(QFile::remove(tree.filePath("__proto__")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 0);
        QCOMPARE(tab->property("anchorIndex").toInt(), -1);
        QCOMPARE(tab->property("statusText").toString(), QStringLiteral("1 item"));
        tab->setProperty("sortDescending", false);
    }
    settings->setUseTreeView(false);
}

static void checkTrashFallback(QQmlApplicationEngine &engine, QObject *window, QQuickItem *tab)
{
    TempTree tree;
    const QString selected = tree.writeFile("selected");
    const QString copying = tree.writeFile("copying");
    tab->setProperty("path", tree.path());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, "selected")));
    QObject *dialog = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (child->property("message").toString() == "These files can't be moved to the trash.")
            dialog = child;
    }
    QVERIFY(dialog);
    auto *ops = engine.singletonInstance<FileOperations *>("Omanta", "FileOperations");
    QVERIFY(ops);
    ops->copy({copying}, "omanta-test-unsupported://host/destination");
    QTRY_VERIFY(!ops->busy());
    QVERIFY(!ops->lastError().isEmpty());
    QVERIFY(!dialog->property("visible").toBool());
    const QString unsupported = "omanta-test-unsupported://host/original";
    QObject otherWindow;
    ops->trash({unsupported}, &otherWindow);
    QTRY_VERIFY(!ops->busy());
    QVERIFY(!dialog->property("visible").toBool());
    ops->trash({unsupported}, window);
    QTRY_VERIFY(!ops->busy());
    QTRY_VERIFY(dialog->property("visible").toBool());
    const QVariant pending = dialog->property("pending");
    QCOMPARE(pending.metaType() == QMetaType::fromType<QJSValue>()
        ? pending.value<QJSValue>().toVariant().toStringList() : pending.toStringList(),
        QStringList{unsupported});
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(QFileInfo::exists(selected));
    QVERIFY(QFileInfo::exists(copying));
}

// GitHub #6: the information and settings dialogs close from the mouse too —
// their ✕, or a press on the dimmed window around them. A dialog that takes
// input keeps Escape and its own buttons, so a stray click can't discard it.
static QObject *findDialog(QObject *window, const char *type)
{
    for (QObject *child : window->findChildren<QObject *>()) {
        if (QByteArray(child->metaObject()->className()).startsWith(type))
            return child;
    }
    return nullptr;
}

static void checkDialogsCloseByMouse(QQuickWindow *window)
{
    const QPoint outside(4, window->height() - 4);
    for (const char *type : {"AboutDialog", "PreferencesDialog", "ShortcutsDialog",
                             "VisibleColumnsDialog"}) {
        QObject *dialog = findDialog(window, type);
        QVERIFY2(dialog, type);
        auto *close = qobject_cast<QQuickItem *>(dialog->property("closeButton").value<QObject *>());
        QVERIFY2(close, type);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY2(dialog->property("opened").toBool(), type);
        QVERIFY2(close->isVisible(), type);
        const QPointF corner = close->mapToScene(QPointF(close->width() / 2, close->height() / 2));
        auto *background = qobject_cast<QQuickItem *>(dialog->property("background").value<QObject *>());
        const QRectF frame(background->mapToScene(QPointF(0, 0)), background->size());
        QVERIFY2(frame.contains(corner), type); // inside the dialog, not off in the dim
        QVERIFY2(corner.x() > frame.center().x() && corner.y() < frame.center().y(), type);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, corner.toPoint());
        QTRY_VERIFY2(!dialog->property("visible").toBool(), type);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY2(dialog->property("opened").toBool(), type);
        QVERIFY2(!frame.contains(outside), type);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, outside);
        QTRY_VERIFY2(!dialog->property("visible").toBool(), type);
    }

    QObject *confirm = findDialog(window, "ConfirmDialog");
    QVERIFY(confirm);
    QVERIFY(QMetaObject::invokeMethod(confirm, "open"));
    QTRY_VERIFY(confirm->property("opened").toBool());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, outside);
    QTest::qWait(50);
    QVERIFY(confirm->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!confirm->property("visible").toBool());
}

// GitHub #5: wide, F9 (toggleSidebar) flips the saved setting; narrow, the
// sidebar hides itself and toggling slides it over the files without touching
// the setting, and picking a place, a press on the dimmed files or Escape
// slides it away again.
static void checkSidebarNarrowing(QQuickWindow *window, Settings *settings)
{
    const auto visible = [window] { return window->property("sidebarVisible").toBool(); };
    const auto overlay = [window] { return window->property("sidebarOverlayOpen").toBool(); };
    QObject *sidebar = findDialog(window, "Sidebar");
    QVERIFY(sidebar);
    const QSize wide = window->size();
    QVERIFY(wide.width() >= 720);
    QVERIFY(visible());
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(!visible());
    QCOMPARE(settings->showSidebar(), false);
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(visible());
    QCOMPARE(settings->showSidebar(), true);

    window->resize(600, wide.height());
    QTRY_VERIFY(window->property("sidebarNarrow").toBool());
    QVERIFY(!visible());
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(visible() && overlay());
    QCOMPARE(settings->showSidebar(), true);
    const QString path = window->property("currentPath").toString();
    QVERIFY(QMetaObject::invokeMethod(sidebar, "navigateRequested", Q_ARG(QString, path)));
    QVERIFY(!overlay());

    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(overlay());
    QTest::qWait(250); // let the slide finish
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      QPoint(window->width() - 40, window->height() / 2));
    QTRY_VERIFY(!overlay());

    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(overlay());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!overlay());

    // Opened while narrow, then widened: back to the saved setting.
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    window->resize(wide);
    QTRY_VERIFY(!window->property("sidebarNarrow").toBool());
    QVERIFY(!overlay());
    QVERIFY(visible());
}

static void collectItems(QQuickItem *item, const char *property, const QVariant &value,
                         QList<QQuickItem *> &found)
{
    if (item->property(property) == value)
        found.append(item);
    for (QQuickItem *child : item->childItems())
        collectItems(child, property, value, found);
}

// Reported by email 2026-09-27: a long name and its copy were
// indistinguishable. One selected item is named in the status line, and in
// the icon view its whole name is shown over the cells below; a
// multi-selection keeps the count and shows no overlay.
static void checkSelectedNameShown(QQuickItem *tab, const QString &longName, const QString &other)
{
    const auto fullNames = [tab] {
        QList<QQuickItem *> all;
        collectItems(tab, "objectName", QStringLiteral("fullName"), all);
        QList<QQuickItem *> shown;
        for (QQuickItem *item : all) {
            if (item->isVisible())
                shown.append(item);
        }
        return shown;
    };
    tab->setProperty("viewMode", "icon");
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, longName)));
    const QString status = tab->property("statusText").toString();
    QVERIFY2(status.startsWith(QStringLiteral("“") + longName + QStringLiteral("” selected (")),
             qPrintable(status));
    QTRY_COMPARE(fullNames().size(), 1);

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, other)));
    QTRY_COMPARE(fullNames().size(), 0); // short name: nothing to expand
    QVERIFY(tab->property("statusText").toString().startsWith(QStringLiteral("“") + other));

    QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
    QVERIFY(tab->property("statusText").toString().endsWith(QStringLiteral(" selected")));
    QTRY_COMPARE(fullNames().size(), 0);
    QVERIFY(QMetaObject::invokeMethod(tab, "clearSelection"));
    tab->setProperty("viewMode", "list");
}

static void checkMountQuestion(QQuickWindow *window)
{
    auto *mounter = window->findChild<Mounter *>();
    QVERIFY(mounter);
    GMountOperation *operation = mounter->createOperation();
    int reply = -1;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    g_signal_connect(operation, "reply", G_CALLBACK(+[](GMountOperation *, GMountOperationResult result,
                                                        gpointer data) {
        *static_cast<int *>(data) = int(result);
    }), &reply);
    const char *choices[] = {"Reject test host", "Trust test host", nullptr};
    g_signal_emit_by_name(operation, "ask-question", "Verify <untrusted> host fingerprint", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    auto *button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    QTest::qWait(20);
    QCOMPARE(reply, -1); // no trust decision until the user acts
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_ABORTED));
    QTRY_VERIFY(!button->isVisible());
    reply = -1;
    g_signal_emit_by_name(operation, "ask-question", "Verify <untrusted> host fingerprint", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_HANDLED));
    QCOMPARE(g_mount_operation_get_choice(operation), 1);
    QTRY_VERIFY(!button->isVisible());

    reply = -1;
    g_signal_emit_by_name(operation, "ask-question", "Device disappeared", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    g_signal_emit_by_name(operation, "aborted");
    QTRY_VERIFY(!button->isVisible());
    QCOMPARE(reply, -1);

    g_signal_emit_by_name(operation, "ask-password", "Password", "user", "", G_ASK_PASSWORD_NEED_PASSWORD);
    auto *password = findItem(window->contentItem(), "placeholderText", QString("Password"));
    QVERIFY(password);
    QTRY_VERIFY(password->isVisible());
    password->setProperty("text", "temporary-secret");
    g_signal_emit_by_name(operation, "aborted");
    QTRY_VERIFY(!password->isVisible());
    QCOMPARE(password->property("text").toString(), QString());
    QCOMPARE(reply, -1);
    // Both aborts release the state, allowing the next real prompt to work.
    g_signal_emit_by_name(operation, "ask-password", "Password", "user", "", G_ASK_PASSWORD_NEED_PASSWORD);
    QTRY_VERIFY(password->isVisible());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_ABORTED));
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

    checkSelectedNameShown(tab, names.first(), names.at(1));
    if (QTest::currentTestFailed())
        return;
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
    tab->setProperty("searchQuery", "");
    // Show-hidden is a setting (#3): the tab follows it, and a toggle in the
    // tab (Ctrl+H, the menus) writes it back for every tab and the next run.
    auto *settings = engine.singletonInstance<Settings *>("Omanta", "Settings");
    QVERIFY(settings);
    QCOMPARE(tab->property("showHidden").toBool(), false);
    settings->setShowHiddenFiles(true);
    QTRY_COMPARE(tab->property("showHidden").toBool(), true);
    tab->setProperty("showHidden", false);
    QCOMPARE(settings->showHiddenFiles(), false);
    settings->setShowHiddenFiles(true); // still following after its own write
    QTRY_COMPARE(tab->property("showHidden").toBool(), true);
    settings->setShowHiddenFiles(false);
    QTRY_COMPARE(tab->property("showHidden").toBool(), false);
    checkSidebarNarrowing(qobject_cast<QQuickWindow *>(window), settings);
    if (QTest::currentTestFailed())
        return;
    checkDialogsCloseByMouse(qobject_cast<QQuickWindow *>(window));
    if (QTest::currentTestFailed())
        return;
    checkMountQuestion(qobject_cast<QQuickWindow *>(window));
    if (QTest::currentTestFailed())
        return;
    checkTrashFallback(engine, window, tab);
    if (QTest::currentTestFailed())
        return;
    checkLiveSelection(qobject_cast<QQuickWindow *>(window), tab,
                       engine.singletonInstance<Settings *>("Omanta", "Settings"));

}

void TestQmlViews::emptyTrashRefreshesOpenViews()
{
    if (qEnvironmentVariable("OMANTA_TEST_TRASH_SANDBOX") != QLatin1String("1")) {
        const QString bwrap = QStandardPaths::findExecutable("bwrap");
        if (bwrap.isEmpty() || !QFileInfo::exists("/usr/lib/gvfsd-trash"))
            QSKIP("The isolated Trash integration test requires bubblewrap and gvfs");

        // Empty Trash must never touch the developer's files. The child gets
        // a fresh filesystem, home, runtime directory and private D-Bus/GVfs.
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(bwrap, {
            "--unshare-all", "--die-with-parent",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
            "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
            "--symlink", "usr/lib", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/omanta-test-home", "--dir", "/run/test",
            "--setenv", "HOME", "/omanta-test-home",
            "--setenv", "XDG_DATA_HOME", "/omanta-test-home/.local/share",
            "--setenv", "XDG_CONFIG_HOME", "/omanta-test-home/.config",
            "--setenv", "XDG_CACHE_HOME", "/omanta-test-home/.cache",
            "--setenv", "XDG_RUNTIME_DIR", "/run/test",
            "--setenv", "OMANTA_TEST_TRASH_SANDBOX", "1",
            "--setenv", "QT_QPA_PLATFORM", "offscreen",
            "--unsetenv", "QT_QPA_PLATFORMTHEME",
            "--unsetenv", "DISPLAY", "--unsetenv", "WAYLAND_DISPLAY",
            "--setenv", "QT_QUICK_BACKEND", "software",
            "--ro-bind", QCoreApplication::applicationFilePath(), "/test",
            "--chdir", "/omanta-test-home",
            "dbus-run-session", "--", "/test", "emptyTrashRefreshesOpenViews"
        });
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(30000));
        const QByteArray output = child.readAll();
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
            qWarning().noquote() << output;
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 output.right(2500).constData());
        return;
    }

    QCOMPARE(QDir::homePath(), QStringLiteral("/omanta-test-home"));
    QVERIFY(QDir().mkpath(QDir::homePath() + "/.local/share/Trash/files"));
    QVERIFY(QDir().mkpath(QDir::homePath() + "/.local/share/Trash/info"));
    // The original bug calls g_file_get_child(trash, "/") for the root's
    // attribute notification, then leaves the removal batch unfinished.
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(QStringLiteral("trash:///"));
    QList<QQuickWindow *> windows;
    for (QObject *child : application.children()) {
        if (auto *window = qobject_cast<QQuickWindow *>(child))
            windows << window;
    }
    QCOMPARE(windows.size(), 1);
    auto *window = windows.first();
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    auto *model = tab->findChild<DirectoryModel *>();
    QVERIFY(model);
    QTRY_VERIFY(!model->loading());
    QVERIFY2(model->errorMessage().isEmpty(), qPrintable(model->errorMessage()));
    QCOMPARE(model->count(), 0);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    auto *ops = engine.singletonInstance<FileOperations *>("Omanta", "FileOperations");
    auto *settings = engine.singletonInstance<Settings *>("Omanta", "Settings");
    QVERIFY(ops);
    QVERIFY(settings);
    QObject *confirmation = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (child->property("confirmText").toString() == QLatin1String("Empty Trash")) {
            confirmation = child;
            break;
        }
    }
    QVERIFY(confirmation);

    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon"), QStringLiteral("tree")}) {
        settings->setUseTreeView(mode == QLatin1String("tree"));
        tab->setProperty("viewMode", mode == QLatin1String("icon") ? "icon" : "list");
        TempTree tree(TempTree::UnderHome);
        const QString first = tree.writeFile("first.txt");
        const QString second = tree.writeFile("with spaces.txt");
        tree.writeFile("folder/child.txt");
        ops->trash({first, second, tree.filePath("folder")});
        QTRY_VERIFY(!ops->busy());
        QVERIFY2(ops->lastError().isEmpty(), qPrintable(ops->lastError()));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);

        QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
        QCOMPARE(tab->property("selectionCount").toInt(), 3);
        QVERIFY(QMetaObject::invokeMethod(confirmation, "open"));
        QTRY_VERIFY(confirmation->property("visible").toBool());
        auto *button = findItem(window->contentItem(), "text", QStringLiteral("Empty Trash"));
        QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        QTRY_VERIFY(!ops->busy());
        QVERIFY2(ops->lastError().isEmpty(), qPrintable(ops->lastError()));
        QTRY_COMPARE(model->count(), 0);
        QTRY_COMPARE(window->property("visibleCount").toInt(), 0);
        QCOMPARE(tab->property("path").toString(), QStringLiteral("trash:///"));
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 0);
        QCOMPARE(tab->property("statusText").toString(), QStringLiteral("0 items"));
        QCOMPARE(resets.count(), 0);
    }
}

QTEST_MAIN(TestQmlViews)
// Copy, then paste: the copied files stay on the clipboard, so they can be
// pasted again (into another folder, or here as a second copy). Only a cut is
// used up by its paste. The bug this guards cleared the clipboard after every
// paste, because paste() passed "from a cut" as true unconditionally.
void TestQmlViews::pasteKeepsCopiedFilesOnClipboard()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"OMANTA_SETTINGS_FILE", "OMANTA_STARRED_FILE",
                           "OMANTA_SERVERS_FILE", "OMANTA_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("OMANTA_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    const QString copied = tree.writeFile("source/copied.txt");
    const QString cut = tree.writeFile("source/cut.txt");
    QVERIFY(QDir().mkpath(tree.filePath("target")));

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.filePath("target"));
    QObject *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = child;
            break;
        }
    }
    QVERIFY(window);
    auto *clipboard = engine.singletonInstance<Clipboard *>("Omanta", "Clipboard");
    auto *operations = engine.singletonInstance<QObject *>("Omanta", "FileOperations");
    QVERIFY(clipboard);
    QVERIFY(operations);

    clipboard->copyFiles({copied});
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("target/copied.txt")));
    QTRY_VERIFY(!operations->property("busy").toBool());
    QVERIFY(QFileInfo::exists(copied));
    QCOMPARE(clipboard->paths(), QStringList{copied});
    QVERIFY(!clipboard->isCut());

    // A second paste of the same copy lands too. The name is taken now, so
    // it waits on the conflict dialog; answer Keep both, as a person would.
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(window->property("pendingTransfer").value<QJSValue>().isObject());
    QVERIFY(QMetaObject::invokeMethod(window, "performTransfer",
                                      Q_ARG(QVariant, int(FileOperations::RenameNew))));
    QTRY_COMPARE(QDir(tree.filePath("target")).entryList(QDir::Files).size(), 2);
    QTRY_VERIFY(!operations->property("busy").toBool());
    QCOMPARE(clipboard->paths(), QStringList{copied});

    clipboard->cutFiles({cut});
    // Reported by email 2026-09-27: a cut looked like a copy until the paste.
    // Cut files are dimmed in both views; a copied one never is.
    QVERIFY(clipboard->isCutPath(cut));
    QVERIFY(!clipboard->isCutPath(copied));
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    const auto dimmed = [tab](const QString &path) {
        QQuickItem *delegate = findFileRow(tab, path);
        if (!delegate)
            return -1;
        for (QQuickItem *child : delegate->childItems()) {
            if (qFuzzyCompare(child->opacity(), 0.5))
                return 1;
        }
        return 0;
    };
    tab->setProperty("path", tree.filePath("source"));
    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        QTRY_COMPARE(dimmed(cut), 1);
        QTRY_COMPARE(dimmed(copied), 0);
    }
    tab->setProperty("path", tree.filePath("target"));
    QTRY_COMPARE(tab->property("path").toString(), tree.filePath("target"));
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("target/cut.txt")));
    QTRY_VERIFY(!operations->property("busy").toBool());
    QVERIFY(!QFileInfo::exists(cut));
    QVERIFY(clipboard->paths().isEmpty());
    QVERIFY(!clipboard->isCutPath(cut));
}

void TestQmlViews::thumbnailsFollowInPlaceEdits()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"OMANTA_SETTINGS_FILE", "OMANTA_STARRED_FILE",
                           "OMANTA_SERVERS_FILE", "OMANTA_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("OMANTA_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());

    // Every edit is stamped with the same whole second: a picture re-saved
    // within the second it was made must still get a fresh preview.
    const QDateTime stamp = QDateTime::currentDateTime().addSecs(-60);
    const auto writePicture = [&](const QString &name, int w, int h) {
        QImage image(w, h, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QVERIFY(image.save(tree.filePath(name), "png"));
        tree.setModified(name, stamp);
    };
    // '#', '%' and '?' would otherwise be read as URL syntax.
    const QStringList names{QStringLiteral("edited.png"),
                            QStringLiteral("shot #1 100% done?.png")};
    for (const QString &name : names)
        writePicture(name, 120, 40);

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
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);

    // Landscape before the edit, portrait after: the painted shape tells the
    // old picture from the new one without reading pixels back.
    // Visible only: a view mode switch leaves the old view's delegates in
    // the tree until the Loader's deferred delete runs.
    const auto preview = [&](const QString &name) {
        const std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *item) -> QQuickItem * {
            if (!item->isVisible())
                return nullptr;
            if (item->property("previewPath") == tree.filePath(name))
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (auto *found = find(child))
                    return found;
            }
            return nullptr;
        };
        return find(tab);
    };
    const auto isThumbnail = [](QQuickItem *item) {
        return item && item->property("source").toUrl().scheme() == QLatin1String("image")
            && item->property("source").toUrl().host() == QLatin1String("thumbnail")
            && item->property("status").toInt() == 1; // Image.Ready
    };
    const auto isLandscape = [](QQuickItem *item) {
        return item->property("paintedWidth").toReal() > item->property("paintedHeight").toReal();
    };

    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        for (const QString &name : names) {
            writePicture(name, 120, 40);
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + ": " + name));
            QTRY_VERIFY2(isLandscape(preview(name)), qPrintable(mode + ": " + name));
        }

        // The file monitor alone must bring the new picture in. Same whole
        // second, so it is the size (and millisecond mtime, where the
        // filesystem keeps one) that tells the versions apart; a different
        // pixel count guarantees the bytes differ.
        for (const QString &name : names) {
            const qint64 before = QFileInfo(tree.filePath(name)).size();
            writePicture(name, 40, 160);
            QVERIFY(QFileInfo(tree.filePath(name)).size() != before);
        }
        for (const QString &name : names) {
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + " live: " + name));
            QTRY_VERIFY2(!isLandscape(preview(name)), qPrintable(mode + " live: " + name));
        }

        // And so must Reload, which rebuilds every row from scratch.
        QVERIFY(QMetaObject::invokeMethod(tab, "reload"));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
        for (const QString &name : names) {
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + " reload: " + name));
            QVERIFY2(!isLandscape(preview(name)), qPrintable(mode + " reload: " + name));
        }
    }
}

void TestQmlViews::dragPreviewSurvivesItsOwner()
{
    // Leaving a view right after pressing a file destroys the drag proxy
    // while its deferred capture check is still queued. That check must die
    // with the proxy instead of running against a torn-down scope.
    QTest::failOnWarning(QRegularExpression("TypeError|is not a function|ReferenceError|invalid context"));
    TempTree tree;
    const QString file = tree.writeFile("pressed.txt");
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Omanta.Runtime", 1, 0, "Theme", &theme);

    // A view delegate, as the list and icon views hold it: navigating away
    // resets the model, which clears the delegate's context straight away
    // and deletes the item later.
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport Omanta\n"
                      "ListView { width: 200; height: 200; model: 1\n"
                      "  delegate: FileDrag { width: 10; height: 10; pressed: true; dragging: false } }",
                      QUrl());
    QScopedPointer<QObject> view(component.create());
    QVERIFY2(view, qPrintable(component.errorString()));
    for (int round = 0; round < 20; ++round) {
        view->setProperty("model", 1);
        QQuickItem *drag = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(view.get(), "itemAtIndex",
                        Q_RETURN_ARG(QQuickItem *, drag), Q_ARG(int, 0)) && drag);
        QVERIFY(QMetaObject::invokeMethod(drag, "prepare",
            Q_ARG(QVariant, QStringList{file}), Q_ARG(QVariant, QStringLiteral("pressed.txt")),
            Q_ARG(QVariant, QString()), Q_ARG(QVariant, QString())));
        view->setProperty("model", 0); // leave the view before the check runs
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
}

// Space previews the selected file through the system previewer (Sushi,
// org.gnome.NautilusPreviewer2): Space again closes it, the preview follows
// the selection while it is up, Space inside a type-ahead name is part of the
// name, and with no previewer the item opens instead with a note saying why.
void TestQmlViews::spacePreviewsInSushi()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("needs a session bus");
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    const QString service = qEnvironmentVariable("OMANTA_PREVIEWER_SERVICE");
    FakePreviewer fake;
    QVERIFY(bus.registerObject(QStringLiteral("/org/gnome/NautilusPreviewer"), &fake,
                               QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
    QVERIFY(bus.registerService(service));
    const auto cleanup = qScopeGuard([&] {
        bus.unregisterService(service);
        bus.unregisterObject(QStringLiteral("/org/gnome/NautilusPreviewer"));
    });

    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"OMANTA_SETTINGS_FILE", "OMANTA_STARRED_FILE",
                           "OMANTA_SERVERS_FILE", "OMANTA_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("OMANTA_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    const QString first = tree.writeFile("alpha.txt");
    const QString second = tree.writeFile("beta notes.md");
    QVERIFY(QDir().mkpath(tree.filePath("folder")));

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
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
    window->requestActivate();
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());
    auto *previewer = engine.singletonInstance<QObject *>("Omanta", "Previewer");
    QVERIFY(previewer);
    const QString firstUri = QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded);
    const QString secondUri = QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded);

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 1);
    QCOMPARE(fake.calls.last().uri, firstUri);
    QVERIFY(fake.calls.last().toggle);
    QTRY_VERIFY(previewer->property("visible").toBool());

    // It follows the selection while up (the URI is encoded: a space in the name).
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("beta notes.md"))));
    QTRY_COMPARE(fake.calls.size(), 2);
    QCOMPARE(fake.calls.last().uri, secondUri);
    QVERIFY(!fake.calls.last().toggle);

    // An arrow pressed inside the preview (GTK_DIR_LEFT) steps the selection
    // back one item, and the preview follows.
    fake.pressInPreview(uint(4)); // what Sushi 50 actually sends
    QTRY_COMPARE(fake.calls.size(), 3);
    QCOMPARE(fake.calls.last().uri, firstUri);
    QCOMPARE(tab->property("selectionCount").toInt(), 1);
    fake.pressInPreview(ushort(5)); // what it declares: right, forward again
    QTRY_COMPARE(fake.calls.size(), 4);
    QCOMPARE(fake.calls.last().uri, secondUri);

    // Closed from the preview window itself (Escape there): the selection
    // stops driving it.
    QVERIFY(QMetaObject::invokeMethod(&fake, "Close"));
    QTRY_VERIFY(!previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 4);

    // Space opens it again, and Space once more closes it.
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 5);
    QTRY_VERIFY(previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("beta notes.md"))));
    QTRY_COMPARE(fake.calls.size(), 6);
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 7);
    QVERIFY(fake.calls.last().toggle);
    QTRY_VERIFY(!previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 7);

    // Mid type-ahead, Space belongs to the name being typed.
    QTest::keyClick(window, Qt::Key_B);
    QTest::keyClick(window, Qt::Key_Space);
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 7);
    QTest::qWait(1000); // the type-ahead prefix times out

    // No previewer: nothing opens — a look-only key must not open or
    // extract (a folder here, so a regression navigates rather than
    // launching an app on the desktop) — and the window says why.
    bus.unregisterService(service);
    QTRY_VERIFY(!previewer->property("visible").toBool()); // its owner left
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("folder"))));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("flashText").toString().contains(QStringLiteral("Sushi")));
    QTest::qWait(200);
    QCOMPARE(tab->property("path").toString(), tree.path());
    QCOMPARE(fake.calls.size(), 7);
}

#include "tst_qmlviews.moc"
