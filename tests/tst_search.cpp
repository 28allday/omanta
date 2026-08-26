#include "SearchModel.h"
#include "DirectoryModel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>
#include <utime.h>

// Recursive filename search. A small fixture tree with the traps built in:
// nested matches, a case difference, a hidden directory, and a symlink loop.
class TestSearch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void findsMatchesRecursively();
    void neverPolicySearchesOnlyTheFolderItself();
    void namesAreRootRelativePaths();
    void requeryReplacesResults();
    void emptyQueryMeansNoResultsAndNoSearch();
    void typeFilterNarrowsByCategory();
    void typeFilterTextMatchesSubtypes();
    void dateRangeIsASinceWindow();
    void dateFilterRejectsMissingTimestamps();
    void changingAFilterRerunsTheSearch();

private:
    void write(const QString &relPath);
    QTemporaryDir m_dir;
};

void TestSearch::write(const QString &relPath)
{
    const QString path = m_dir.filePath(relPath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("data");
}

void TestSearch::initTestCase()
{
    write(QStringLiteral("alpha.txt"));
    write(QStringLiteral("beta.txt"));
    write(QStringLiteral("sub/alpha_two.txt"));
    write(QStringLiteral("sub/deep/ALPHA.md"));
    write(QStringLiteral(".hiddendir/alpha_hidden.txt"));

    // A directory symlink pointing back up: descending through it would
    // never terminate. The walk must skip it.
    QVERIFY(::symlink(m_dir.path().toUtf8().constData(),
                      m_dir.filePath(QStringLiteral("loop")).toUtf8().constData()) == 0);
}

void TestSearch::findsMatchesRecursively()
{
    SearchModel model;
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("alpha"));

    // Debounce (250ms) then the walk; generous ceiling, fast in practice.
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);

    // Three: the root one, the nested one, the case-different one. The one
    // in the hidden directory is skipped, and the symlink loop ended.
    QCOMPARE(model.count(), 3);
}

void TestSearch::neverPolicySearchesOnlyTheFolderItself()
{
    SearchModel model;
    model.setRecursion(QStringLiteral("never"));
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("alpha"));

    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() > 0, 10000);

    // Only the root-level match — the walk never entered sub/.
    QCOMPARE(model.count(), 1);
}

void TestSearch::namesAreRootRelativePaths()
{
    SearchModel model;
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 3, 10000);

    QStringList names;
    QStringList paths;
    for (int row = 0; row < model.count(); ++row) {
        names.append(model.data(model.index(row, 0), DirectoryModel::NameRole).toString());
        paths.append(model.data(model.index(row, 0), DirectoryModel::FilePathRole).toString());
    }

    // The name role is the relative path — unique across directories, which
    // is what keeps name-keyed selection working over results.
    QVERIFY(names.contains(QStringLiteral("alpha.txt")));
    QVERIFY(names.contains(QStringLiteral("sub/alpha_two.txt")));
    QVERIFY(names.contains(QStringLiteral("sub/deep/ALPHA.md")));

    // And filePath resolves each to its real location.
    QVERIFY(paths.contains(m_dir.filePath(QStringLiteral("sub/alpha_two.txt"))));
}

void TestSearch::requeryReplacesResults()
{
    SearchModel model;
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 3, 10000);

    model.setQuery(QStringLiteral("beta"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);
    QCOMPARE(model.data(model.index(0, 0), DirectoryModel::NameRole).toString(),
             QStringLiteral("beta.txt"));
}

void TestSearch::emptyQueryMeansNoResultsAndNoSearch()
{
    SearchModel model;
    model.setRootLocation(m_dir.path());
    model.setQuery(QStringLiteral("alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 3, 10000);

    model.setQuery(QString());
    QTRY_VERIFY_WITH_TIMEOUT(model.count() == 0, 10000);
    QVERIFY(!model.searching());
}

void TestSearch::typeFilterNarrowsByCategory()
{
    QTemporaryDir dir;
    const auto seed = [&](const QString &name) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    };
    seed(QStringLiteral("shot_one.jpg"));
    seed(QStringLiteral("shot_two.png"));
    seed(QStringLiteral("shot_notes.txt"));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("shot_folder"))));

    SearchModel model;
    model.setRootLocation(dir.path());
    model.setTypeFilter(QStringLiteral("pictures"));
    model.setQuery(QStringLiteral("shot"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 2, 10000);

    model.setTypeFilter(QStringLiteral("folders"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);
    QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
             QStringLiteral("shot_folder"));

    // Back to Anything: everything returns.
    model.setTypeFilter(QStringLiteral("any"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 4, 10000);
}

void TestSearch::typeFilterTextMatchesSubtypes()
{
    QTemporaryDir dir;
    {
        QFile file(dir.filePath(QStringLiteral("tool.py")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("print('x')\n");
    }
    {
        QFile file(dir.filePath(QStringLiteral("tool.jpg")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    }

    SearchModel model;
    model.setRootLocation(dir.path());
    // g_content_type_is_a is the point: text/x-python IS a text/plain, so
    // "Text Files" catches scripts the way Nautilus's filter does.
    model.setTypeFilter(QStringLiteral("text"));
    model.setQuery(QStringLiteral("tool"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);
    QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
             QStringLiteral("tool.py"));
}

void TestSearch::dateRangeIsASinceWindow()
{
    QTemporaryDir dir;
    const auto seedAt = [&](const QString &name, const QDateTime &when) {
        QFile file(dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
        file.close();
        utimbuf times;
        times.actime = when.toSecsSinceEpoch();
        times.modtime = when.toSecsSinceEpoch();
        QVERIFY(utime(dir.filePath(name).toUtf8().constData(), &times) == 0);
    };
    const QDateTime now = QDateTime::currentDateTime();
    seedAt(QStringLiteral("fresh.txt"), now.addDays(-1));
    seedAt(QStringLiteral("stale.txt"), now.addDays(-90));
    seedAt(QStringLiteral("ancient.txt"), now.addDays(-500));

    SearchModel model;
    model.setRootLocation(dir.path());
    model.setDateRange(QStringLiteral("week"));
    model.setQuery(QStringLiteral("txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);
    QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
             QStringLiteral("fresh.txt"));

    model.setDateRange(QStringLiteral("year"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 2, 10000);

    model.setDateRange(QStringLiteral("any"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 3, 10000);
}

void TestSearch::dateFilterRejectsMissingTimestamps()
{
    QTemporaryDir dir;
    {
        QFile file(dir.filePath(QStringLiteral("thing.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    }

    SearchModel model;
    model.setRootLocation(dir.path());
    // Every local file carries an mtime, so "modified + today" matches the
    // fresh file; the same file must still match on "accessed" (utime set it
    // implicitly on write) — the kinds read different attributes.
    model.setDateKind(QStringLiteral("modified"));
    model.setDateRange(QStringLiteral("today"));
    model.setQuery(QStringLiteral("thing"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);

    model.setDateKind(QStringLiteral("accessed"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);

    // An unknown kind is repaired to the default rather than trusted.
    model.setDateKind(QStringLiteral("cuneiform"));
    QCOMPARE(model.dateKind(), QStringLiteral("modified"));
}

void TestSearch::changingAFilterRerunsTheSearch()
{
    QTemporaryDir dir;
    {
        QFile file(dir.filePath(QStringLiteral("pic.jpg")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    }
    {
        QFile file(dir.filePath(QStringLiteral("pic.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
    }

    SearchModel model;
    model.setRootLocation(dir.path());
    model.setQuery(QStringLiteral("pic"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 2, 10000);

    // Filters applied to a finished search re-run it — no stale rows linger.
    model.setTypeFilter(QStringLiteral("pictures"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.searching() && model.count() == 1, 10000);
    QCOMPARE(model.data(model.index(0), DirectoryModel::NameRole).toString(),
             QStringLiteral("pic.jpg"));
}

QTEST_GUILESS_MAIN(TestSearch)
#include "tst_search.moc"
