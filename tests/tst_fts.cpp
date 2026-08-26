#include "SearchModel.h"
#include "DirectoryModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <tinysparql.h>

// Content (full-text) search. The session's localsearch index is nobody's
// fixture — it holds whatever this machine holds — so these tests build a
// private in-process tinysparql store with the same Nepomuk ontology the
// indexer uses, insert triples describing real files on disk, and inject the
// connection into SearchModel. The model's whole content path — statement
// binding, cursor walk, URL scoping, the GIO stat of each hit — runs exactly
// as it does against the real index.
class TestFts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void findsContentMatch();
    void namesAreRootRelativePaths();
    void scopesToTheRoot();
    void deletedFileIsDropped();
    void modeFlipSwitchesEngines();
    void nonLocalRootIsUnavailable();
    void emptyQueryMeansNoResultsAndNoSearch();

private:
    QString write(const QString &relPath, const QByteArray &content);
    void index(const QString &path, const QString &text);

    QTemporaryDir m_dir;
    QTemporaryDir m_storeDir;
    TrackerSparqlConnection *m_conn = nullptr;
    int m_urn = 0;
};

QString TestFts::write(const QString &relPath, const QByteArray &content)
{
    const QString path = m_dir.filePath(relPath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(content);
    return path;
}

// One file's worth of what the miner would have extracted: the DataObject
// with its URL, the InformationElement with the full-text content.
void TestFts::index(const QString &path, const QString &text)
{
    GFile *file = g_file_new_for_path(path.toUtf8().constData());
    char *uri = g_file_get_uri(file);

    char *update = g_strdup_printf(
        "INSERT DATA {"
        "  <urn:tst:f%1$d> a nfo:FileDataObject ; nie:url \"%2$s\" ."
        "  <urn:tst:ie%1$d> a nfo:PlainTextDocument ;"
        "    nie:isStoredAs <urn:tst:f%1$d> ;"
        "    nie:plainTextContent \"%3$s\" ."
        "}",
        ++m_urn, uri, text.toUtf8().constData());

    GError *error = nullptr;
    tracker_sparql_connection_update(m_conn, update, nullptr, &error);
    QVERIFY2(!error, error ? error->message : "");

    g_free(update);
    g_free(uri);
    g_object_unref(file);
}

void TestFts::initTestCase()
{
    GFile *store = g_file_new_for_path(m_storeDir.path().toUtf8().constData());
    GError *error = nullptr;
    m_conn = tracker_sparql_connection_new(TRACKER_SPARQL_CONNECTION_FLAGS_NONE,
                                           store, tracker_sparql_get_ontology_nepomuk(),
                                           nullptr, &error);
    g_object_unref(store);
    QVERIFY2(m_conn, error ? error->message : "");
    SearchModel::setConnectionForTesting(m_conn);

    index(write(QStringLiteral("notes.txt"), "data"),
          QStringLiteral("the quokka ate my homework"));
    index(write(QStringLiteral("sub/deep.txt"), "data"),
          QStringLiteral("a quokka in a subfolder"));
    index(write(QStringLiteral("other.txt"), "data"),
          QStringLiteral("nothing relevant here"));
}

void TestFts::cleanupTestCase()
{
    SearchModel::setConnectionForTesting(nullptr);
    if (m_conn) {
        tracker_sparql_connection_close(m_conn);
        g_object_unref(m_conn);
    }
}

void TestFts::findsContentMatch()
{
    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("quokka"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);
    QCOMPARE(model.count(), 2);
    QVERIFY(model.unavailableReason().isEmpty());
}

void TestFts::namesAreRootRelativePaths()
{
    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("subfolder"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), DirectoryModel::NameRole).toString(),
             QStringLiteral("sub/deep.txt"));
    QCOMPARE(model.data(model.index(0, 0), DirectoryModel::FilePathRole).toString(),
             m_dir.filePath(QStringLiteral("sub/deep.txt")));
}

void TestFts::scopesToTheRoot()
{
    // Searching under sub/ must not surface the match at the root.
    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(m_dir.filePath(QStringLiteral("sub")));
    model.setQuery(QStringLiteral("quokka"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), DirectoryModel::NameRole).toString(),
             QStringLiteral("deep.txt"));
}

void TestFts::deletedFileIsDropped()
{
    // A hit the index still holds but the disk no longer does: the stat
    // fails and the row is silently dropped, not shown as a ghost.
    const QString path = write(QStringLiteral("gone.txt"), "data");
    index(path, QStringLiteral("ephemeral wombat"));
    QVERIFY(QFile::remove(path));

    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("wombat"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching(), 10000);
    QCOMPARE(model.count(), 0);
}

void TestFts::modeFlipSwitchesEngines()
{
    // Same model, same query string: filename mode matches names, content
    // mode matches text. "notes" appears in a filename but in no content.
    SearchModel model;
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("notes"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);
    QCOMPARE(model.count(), 1);

    model.setContentMode(true);
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching(), 10000);
    QCOMPARE(model.count(), 0);

    model.setContentMode(false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);
    QCOMPARE(model.count(), 1);
}

void TestFts::nonLocalRootIsUnavailable()
{
    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(QStringLiteral("trash:///"));
    model.setQuery(QStringLiteral("quokka"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && !model.unavailableReason().isEmpty(), 10000);
    QCOMPARE(model.count(), 0);
}

void TestFts::emptyQueryMeansNoResultsAndNoSearch()
{
    SearchModel model;
    model.setContentMode(true);
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("   "));

    QTest::qWait(400); // past the debounce
    QVERIFY(!model.searching());
    QCOMPARE(model.count(), 0);
}

QTEST_GUILESS_MAIN(TestFts)
#include "tst_fts.moc"
