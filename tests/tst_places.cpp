#include "PlacesModel.h"
#include "TestFixture.h"

#include <gio/gio.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// The sidebar's model. The deterministic parts: the fixed Places section,
// GTK bookmark parsing, the live re-derive when the bookmarks file changes,
// and section ordering. Devices depend on the machine's GVolumeMonitor and
// are only sanity-checked, never counted.
class TestPlaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void placesSectionHasTheFixedEntries();
    void sectionsAreContiguousAndOrdered();
    void parsesGtkBookmarks();
    void bookmarkEditsAreFollowedLive();
    void findsRowForLocation();
    void trashIconTracksContents();

    void addBookmarkWritesTheGtkFile();
    void addBookmarkRefusesDuplicates();
    void removeBookmarkKeepsTheOthersAndTheirLabels();
    void bookmarkWritesRefreshTheModel();

private:
    QString writeBookmarks(const QString &content);
    QTemporaryDir m_dir;
};

QString TestPlaces::writeBookmarks(const QString &content)
{
    const QString path = m_dir.filePath(QStringLiteral("bookmarks"));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    file.write(content.toUtf8());
    file.close();
    return path;
}

void TestPlaces::initTestCase()
{
    // The constructor once called devicesSection() before the volume monitor
    // existed — a GLib CRITICAL and a permanently empty Devices section, and
    // nothing else visible. Fatal criticals turn that mistake into a failure.
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

    qputenv("OMANTA_BOOKMARKS_FILE",
            writeBookmarks(QStringLiteral("file:///tmp Scratch\n"
                                          "file:///var/log\n"
                                          "smb://server/share Media\n")).toUtf8());
}

static QStringList namesInSection(PlacesModel &model, const QString &section)
{
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (model.data(index, PlacesModel::SectionRole).toString() == section)
            names.append(model.data(index, PlacesModel::NameRole).toString());
    }
    return names;
}

void TestPlaces::placesSectionHasTheFixedEntries()
{
    PlacesModel model;
    const QStringList names = namesInSection(model, QStringLiteral("Places"));

    // Nautilus's exact rows in Nautilus's exact order — verified against
    // Files 50.2.2 on the target machine. Nothing added (no XDG dirs — those
    // arrive as bookmarks), nothing dropped, nothing reordered.
    QCOMPARE(names, QStringList({ QStringLiteral("Home"), QStringLiteral("Recent"),
                                  QStringLiteral("Starred"), QStringLiteral("Network"),
                                  QStringLiteral("Trash") }));

    QVERIFY(model.rowForLocation(QDir::homePath()) >= 0);
    QVERIFY(model.rowForLocation(QStringLiteral("trash:///")) >= 0);
    QVERIFY(model.rowForLocation(QStringLiteral("recent:///")) >= 0);
    QVERIFY(model.rowForLocation(QStringLiteral("starred:///")) >= 0);
    QVERIFY(model.rowForLocation(QStringLiteral("network:///")) >= 0);
}

void TestPlaces::sectionsAreContiguousAndOrdered()
{
    PlacesModel model;
    const QStringList order{ QStringLiteral("Places"), QStringLiteral("Bookmarks"),
                             QStringLiteral("Devices") };

    int highest = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QString section =
            model.data(model.index(row, 0), PlacesModel::SectionRole).toString();
        const int rank = int(order.indexOf(section));
        QVERIFY(rank >= 0);
        QVERIFY2(rank >= highest, "sections must not interleave");
        highest = rank;
    }
}

void TestPlaces::parsesGtkBookmarks()
{
    PlacesModel model;
    const QStringList names = namesInSection(model, QStringLiteral("Bookmarks"));

    // A label when given, the location's own name when not, URIs welcome.
    QCOMPARE(names, QStringList({ QStringLiteral("Scratch"), QStringLiteral("log"),
                                  QStringLiteral("Media") }));

    // The file:// form must have normalized to a plain path.
    QVERIFY(model.rowForLocation(QStringLiteral("/tmp")) >= 0);
}

void TestPlaces::bookmarkEditsAreFollowedLive()
{
    PlacesModel model;
    QVERIFY(!namesInSection(model, QStringLiteral("Bookmarks")).isEmpty());

    QSignalSpy spy(&model, &PlacesModel::countChanged);
    writeBookmarks(QStringLiteral("file:///tmp Only One\n"));

    QVERIFY(spy.wait(5000));
    QCOMPARE(namesInSection(model, QStringLiteral("Bookmarks")),
             QStringList({ QStringLiteral("Only One") }));

    // The sections around it survived the splice untouched.
    QCOMPARE(namesInSection(model, QStringLiteral("Places")).first(), QStringLiteral("Home"));
    QCOMPARE(namesInSection(model, QStringLiteral("Places")).last(), QStringLiteral("Trash"));
}

void TestPlaces::findsRowForLocation()
{
    PlacesModel model;
    QVERIFY(model.rowForLocation(QDir::homePath()) >= 0);
    QCOMPARE(model.rowForLocation(QStringLiteral("/no/such/place")), -1);
    QCOMPARE(model.rowForLocation(QString()), -1);
}

static QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

// The sidebar can flips to the full variant while the trash holds anything.
// Trashing is real (a file under $HOME through g_file_trash), so this drives
// the monitor + item-count path end to end; the trashed file is removed from
// the trash afterwards. The empty→full edge is only observable on a machine
// whose trash starts empty, so the reverse edge is asserted conditionally.
void TestPlaces::trashIconTracksContents()
{
    auto iconOf = [](PlacesModel &model) {
        const int row = model.rowForLocation(QStringLiteral("trash:///"));
        return model.data(model.index(row, 0), PlacesModel::IconSourceRole).toString();
    };
    auto trashCount = [] {
        GFile *trash = g_file_new_for_uri("trash:///");
        GFileInfo *info = g_file_query_info(trash, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT,
                                            G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
        g_object_unref(trash);
        if (!info)
            return 0u;
        const guint32 count =
            g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT);
        g_object_unref(info);
        return count;
    };

    PlacesModel model;

    // The initial async query settles to the machine's real state.
    const bool startedFull = trashCount() > 0;
    QTRY_COMPARE(iconOf(model).contains(QLatin1String("full")), startedFull);

    // Trash a real file; the can must show full.
    TempTree tree(TempTree::UnderHome);
    const QString victim = tree.writeFile(QStringLiteral("trash-icon-probe.txt"));
    GFile *file = g_file_new_for_path(victim.toUtf8().constData());
    QVERIFY(g_file_trash(file, nullptr, nullptr));
    g_object_unref(file);
    QTRY_VERIFY(iconOf(model).contains(QLatin1String("full")));

    // Clean up: delete our item out of the trash (match on orig-path).
    GFile *trash = g_file_new_for_uri("trash:///");
    GFileEnumerator *it = g_file_enumerate_children(
        trash, "standard::name,trash::orig-path", G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    QVERIFY(it);
    while (GFileInfo *info = g_file_enumerator_next_file(it, nullptr, nullptr)) {
        const char *orig =
            g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
        if (orig && QString::fromUtf8(orig) == victim) {
            GFile *item = g_file_get_child(trash, g_file_info_get_name(info));
            g_file_delete(item, nullptr, nullptr);
            g_object_unref(item);
        }
        g_object_unref(info);
    }
    g_object_unref(it);
    g_object_unref(trash);

    // Only a trash that is empty again can prove the reverse edge.
    if (!startedFull)
        QTRY_VERIFY(!iconOf(model).contains(QLatin1String("full")));
}

void TestPlaces::addBookmarkWritesTheGtkFile()
{
    const QString path = writeBookmarks(QStringLiteral("file:///tmp Scratch\n"));
    PlacesModel model;

    QVERIFY(!model.isBookmarked(QStringLiteral("/var/log")));
    model.addBookmark(QStringLiteral("/var/log"));

    // Stored in URI form — the format GTK itself writes and Nautilus reads.
    QCOMPARE(readFile(path),
             QStringLiteral("file:///tmp Scratch\nfile:///var/log\n"));
    QVERIFY(model.isBookmarked(QStringLiteral("/var/log")));
}

void TestPlaces::addBookmarkRefusesDuplicates()
{
    const QString path = writeBookmarks(QStringLiteral("file:///tmp Scratch\n"));
    PlacesModel model;

    // Already there, however it is spelled — path or URI.
    model.addBookmark(QStringLiteral("/tmp"));
    model.addBookmark(QStringLiteral("file:///tmp"));

    QCOMPARE(readFile(path), QStringLiteral("file:///tmp Scratch\n"));
}

void TestPlaces::removeBookmarkKeepsTheOthersAndTheirLabels()
{
    const QString path = writeBookmarks(QStringLiteral("file:///tmp Scratch\n"
                                                       "file:///var/log\n"
                                                       "smb://server/share Media\n"));
    PlacesModel model;

    model.removeBookmark(QStringLiteral("/var/log"));

    // The neighbours survive byte-for-byte, labels included.
    QCOMPARE(readFile(path),
             QStringLiteral("file:///tmp Scratch\nsmb://server/share Media\n"));
    QVERIFY(model.isBookmarked(QStringLiteral("/tmp")));
    QVERIFY(!model.isBookmarked(QStringLiteral("/var/log")));
}

void TestPlaces::bookmarkWritesRefreshTheModel()
{
    writeBookmarks(QStringLiteral("file:///tmp Scratch\n"));
    PlacesModel model;
    QCOMPARE(namesInSection(model, QStringLiteral("Bookmarks")).size(), 1);

    QSignalSpy spy(&model, &PlacesModel::countChanged);
    model.addBookmark(QStringLiteral("/var/log"));
    QVERIFY(spy.wait(5000));
    QCOMPARE(namesInSection(model, QStringLiteral("Bookmarks")),
             QStringList({ QStringLiteral("Scratch"), QStringLiteral("log") }));

    model.removeBookmark(QStringLiteral("/var/log"));
    QVERIFY(spy.wait(5000));
    QCOMPARE(namesInSection(model, QStringLiteral("Bookmarks")),
             QStringList({ QStringLiteral("Scratch") }));
}

QTEST_GUILESS_MAIN(TestPlaces)
#include "tst_places.moc"
