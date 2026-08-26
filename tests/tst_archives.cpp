#include "ArchiveEngine.h"
#include "FileOperations.h"
#include "TestFixture.h"

#include <QProcess>
#include <QTest>

#include <gio/gio.h>

#include <unistd.h>

// Compress and extract, asserted on the filesystem. The round-trips go
// through bsdtar as the second opinion where it matters: an archive this
// engine both writes and reads could hide a symmetric bug.
class TestArchives : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL); }

    // engine: compress
    void zipRoundTripsFilesAndFolders();
    void tarXzRoundTrips();
    void sevenZipRoundTrips();
    void bsdtarCanReadOurZip();
    void refusesToOverwriteAnExistingArchive();
    void failedCompressLeavesNoArchive();

    // engine: extract
    void singleTopLevelEntryLandsAsItself();
    void multipleTopLevelEntriesGetAFolder();
    void extractionNeverOverwrites();
    void rawGzipSingleExtractsToTheStem();
    void refusesEntriesThatEscapeTheDestination();
    void corruptArchiveFailsWithNothingLeftBehind();
    void symlinksSurviveTheRoundTrip();

    // Encrypted zip
    void encryptedZipRoundTrips();
    void encryptedZipNeedsThePassword();
    void wrongPasswordAsksAgainNotErrors();
    void bsdtarCanReadOurEncryptedZip();

    // helpers
    void archiveExtensionOffsets();
    void archiveContentTypes();

    // the operations
    void compressAndUndoRemovesTheArchive();
    void extractAndUndoRemovesTheOutput();

private:
    static bool engineCompress(const QStringList &sources, const QString &archive,
                               QString *error)
    {
        return ArchiveEngine::compress(sources, archive, error, [] { return false; },
                                       [](qint64, qint64) {});
    }

    static bool engineExtract(const QString &archive, const QString &dest, QString *produced,
                              QString *error)
    {
        return ArchiveEngine::extract(archive, dest, produced, error, [] { return false; },
                                      [](qint64, qint64) {});
    }

    static bool engineCompressLocked(const QStringList &sources, const QString &archive,
                                     const QString &password, QString *error)
    {
        return ArchiveEngine::compress(sources, archive, error, [] { return false; },
                                       [](qint64, qint64) {}, password);
    }

    static bool engineExtractLocked(const QString &archive, const QString &dest,
                                    const QString &password, QString *produced,
                                    QString *error, bool *needsPassphrase)
    {
        return ArchiveEngine::extract(archive, dest, produced, error, [] { return false; },
                                      [](qint64, qint64) {}, password, needsPassphrase);
    }

    static QString read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static QString write(const TempTree &tree, const QString &name, const QByteArray &content)
    {
        const QString target = tree.filePath(name);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(content);
        return target;
    }

    static bool settle(FileOperations &ops, int timeoutMs = 15000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!ops.busy() && timer.elapsed() < 500)
            QTest::qWait(10);
        while (ops.busy() && timer.elapsed() < timeoutMs)
            QTest::qWait(20);
        return !ops.busy();
    }

    // A round-trip through one format: compress a small tree, extract it
    // elsewhere, and compare contents byte for byte.
    void roundTrip(const QString &archiveName)
    {
        TempTree tree;
        write(tree, "docs/readme.txt", "hello");
        write(tree, "docs/sub/deep.txt", "deep");
        write(tree, "top.txt", "top");

        const QString archive = tree.filePath(archiveName);
        QString error;
        QVERIFY2(engineCompress({ tree.filePath("docs"), tree.filePath("top.txt") },
                                archive, &error),
                 qPrintable(error));
        QVERIFY(QFileInfo::exists(archive));

        TempTree out;
        QString produced;
        QVERIFY2(engineExtract(archive, out.path(), &produced, &error), qPrintable(error));

        // Two top-level entries → a folder named after the archive.
        const QString stem = QFileInfo(archive).fileName().left(
            ArchiveEngine::archiveExtensionOffset(QFileInfo(archive).fileName()));
        QCOMPARE(QFileInfo(produced).fileName(), stem);
        QCOMPARE(read(produced + "/docs/readme.txt"), QStringLiteral("hello"));
        QCOMPARE(read(produced + "/docs/sub/deep.txt"), QStringLiteral("deep"));
        QCOMPARE(read(produced + "/top.txt"), QStringLiteral("top"));
    }
};

void TestArchives::zipRoundTripsFilesAndFolders()
{
    roundTrip(QStringLiteral("bundle.zip"));
}

void TestArchives::tarXzRoundTrips()
{
    roundTrip(QStringLiteral("bundle.tar.xz"));
}

void TestArchives::sevenZipRoundTrips()
{
    roundTrip(QStringLiteral("bundle.7z"));
}

void TestArchives::bsdtarCanReadOurZip()
{
    if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())
        QSKIP("bsdtar not installed");

    TempTree tree;
    write(tree, "a.txt", "aaa");
    QString error;
    QVERIFY2(engineCompress({ tree.filePath("a.txt") }, tree.filePath("x.zip"), &error),
             qPrintable(error));

    QProcess tar;
    tar.start(QStringLiteral("bsdtar"), { QStringLiteral("-tf"), tree.filePath("x.zip") });
    QVERIFY(tar.waitForFinished(5000));
    QCOMPARE(tar.exitCode(), 0);
    QVERIFY(QString::fromUtf8(tar.readAllStandardOutput()).contains(QStringLiteral("a.txt")));
}

void TestArchives::refusesToOverwriteAnExistingArchive()
{
    TempTree tree;
    write(tree, "a.txt", "aaa");
    write(tree, "taken.zip", "not really a zip");

    QString error;
    QVERIFY(!engineCompress({ tree.filePath("a.txt") }, tree.filePath("taken.zip"), &error));
    QCOMPARE(read(tree.filePath("taken.zip")), QStringLiteral("not really a zip"));
}

void TestArchives::failedCompressLeavesNoArchive()
{
    TempTree tree;
    QString error;
    // A source that does not exist fails the batch — and must take the
    // half-written archive with it.
    QVERIFY(!engineCompress({ tree.filePath("missing.txt") }, tree.filePath("out.zip"),
                            &error));
    QVERIFY(!QFileInfo::exists(tree.filePath("out.zip")));
}

void TestArchives::singleTopLevelEntryLandsAsItself()
{
    TempTree tree;
    write(tree, "only/one.txt", "one");
    QString error;
    QVERIFY2(engineCompress({ tree.filePath("only") }, tree.filePath("only.zip"), &error),
             qPrintable(error));

    TempTree out;
    QString produced;
    QVERIFY2(engineExtract(tree.filePath("only.zip"), out.path(), &produced, &error),
             qPrintable(error));

    // One top-level entry ("only/") extracts as itself, no wrapper folder.
    QCOMPARE(QFileInfo(produced).fileName(), QStringLiteral("only"));
    QCOMPARE(read(out.filePath("only/one.txt")), QStringLiteral("one"));
}

void TestArchives::multipleTopLevelEntriesGetAFolder()
{
    TempTree tree;
    write(tree, "a.txt", "a");
    write(tree, "b.txt", "b");
    QString error;
    QVERIFY2(engineCompress({ tree.filePath("a.txt"), tree.filePath("b.txt") },
                            tree.filePath("pair.zip"), &error),
             qPrintable(error));

    TempTree out;
    QString produced;
    QVERIFY2(engineExtract(tree.filePath("pair.zip"), out.path(), &produced, &error),
             qPrintable(error));

    QCOMPARE(QFileInfo(produced).fileName(), QStringLiteral("pair"));
    QCOMPARE(read(out.filePath("pair/a.txt")), QStringLiteral("a"));
    QCOMPARE(read(out.filePath("pair/b.txt")), QStringLiteral("b"));
}

void TestArchives::extractionNeverOverwrites()
{
    TempTree tree;
    write(tree, "only/one.txt", "from archive");
    QString error;
    QVERIFY(engineCompress({ tree.filePath("only") }, tree.filePath("only.zip"), &error));

    TempTree out;
    write(out, "only/one.txt", "already here");

    QString produced;
    QVERIFY2(engineExtract(tree.filePath("only.zip"), out.path(), &produced, &error),
             qPrintable(error));

    // "only" was taken, so the extraction landed as "only 2" — Nautilus's
    // naming — and what was already there is untouched.
    QCOMPARE(QFileInfo(produced).fileName(), QStringLiteral("only 2"));
    QCOMPARE(read(out.filePath("only/one.txt")), QStringLiteral("already here"));
    QCOMPARE(read(out.filePath("only 2/one.txt")), QStringLiteral("from archive"));
}

void TestArchives::rawGzipSingleExtractsToTheStem()
{
    if (QStandardPaths::findExecutable(QStringLiteral("gzip")).isEmpty())
        QSKIP("gzip not installed");

    TempTree tree;
    write(tree, "notes.txt", "just text");
    QProcess gzip;
    gzip.start(QStringLiteral("gzip"), { QStringLiteral("-k"), tree.filePath("notes.txt") });
    QVERIFY(gzip.waitForFinished(5000));
    QVERIFY(QFileInfo::exists(tree.filePath("notes.txt.gz")));

    TempTree out;
    QString produced, error;
    QVERIFY2(engineExtract(tree.filePath("notes.txt.gz"), out.path(), &produced, &error),
             qPrintable(error));
    QCOMPARE(QFileInfo(produced).fileName(), QStringLiteral("notes.txt"));
    QCOMPARE(read(produced), QStringLiteral("just text"));
}

void TestArchives::refusesEntriesThatEscapeTheDestination()
{
    if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())
        QSKIP("bsdtar not installed");

    // A hostile archive with a "../escape.txt" entry, built with bsdtar's -s
    // rewriting so the entry name really contains the dot-dot.
    TempTree tree;
    write(tree, "victim/escape.txt", "gotcha");
    QProcess tar;
    tar.setWorkingDirectory(tree.filePath("victim"));
    tar.start(QStringLiteral("bsdtar"),
              { QStringLiteral("-cf"), tree.filePath("evil.tar"),
                QStringLiteral("-s"), QStringLiteral("|escape.txt|../escape.txt|"),
                QStringLiteral("escape.txt") });
    QVERIFY(tar.waitForFinished(5000));
    QCOMPARE(tar.exitCode(), 0);

    TempTree out;
    QDir().mkpath(out.filePath("inner"));
    QString produced, error;
    QVERIFY(!engineExtract(tree.filePath("evil.tar"), out.filePath("inner"), &produced,
                           &error));
    // Nothing escaped, and nothing was left behind — not even a staging dir.
    QVERIFY(!QFileInfo::exists(out.filePath("escape.txt")));
    QCOMPARE(QDir(out.filePath("inner")).entryList(QDir::AllEntries | QDir::Hidden
                                                   | QDir::NoDotAndDotDot).size(), 0);
}

void TestArchives::corruptArchiveFailsWithNothingLeftBehind()
{
    TempTree tree;
    write(tree, "broken.zip", "this is not a zip file at all");

    TempTree out;
    QString produced, error;
    QVERIFY(!engineExtract(tree.filePath("broken.zip"), out.path(), &produced, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(QDir(out.path()).entryList(QDir::AllEntries | QDir::Hidden
                                        | QDir::NoDotAndDotDot).size(), 0);
}

void TestArchives::symlinksSurviveTheRoundTrip()
{
    TempTree tree;
    write(tree, "linked/real.txt", "real");
    // Not QFile::link — it absolutizes the target, and an absolute symlink
    // would "survive" by still pointing into the source tree.
    QVERIFY(::symlink("real.txt", tree.filePath("linked/alias").toLocal8Bit().constData())
            == 0);

    QString error;
    QVERIFY2(engineCompress({ tree.filePath("linked") }, tree.filePath("l.tar.xz"), &error),
             qPrintable(error));

    TempTree out;
    QString produced;
    QVERIFY2(engineExtract(tree.filePath("l.tar.xz"), out.path(), &produced, &error),
             qPrintable(error));

    const QFileInfo alias(out.filePath("linked/alias"));
    QVERIFY(alias.isSymLink());
    QCOMPARE(alias.symLinkTarget(), out.filePath("linked/real.txt"));
}

void TestArchives::encryptedZipRoundTrips()
{
    TempTree tree;
    write(tree, "secret.txt", "the contents");
    tree.makeDir("out");

    QString error;
    QVERIFY2(engineCompressLocked({ tree.filePath("secret.txt") },
                                  tree.filePath("locked.zip"),
                                  QStringLiteral("hunter2"), &error),
             qPrintable(error));

    QString produced;
    bool needsPassphrase = true;
    QVERIFY2(engineExtractLocked(tree.filePath("locked.zip"), tree.filePath("out"),
                                 QStringLiteral("hunter2"), &produced, &error,
                                 &needsPassphrase),
             qPrintable(error));
    QVERIFY(!needsPassphrase);
    QCOMPARE(read(produced), QStringLiteral("the contents"));
}

void TestArchives::encryptedZipNeedsThePassword()
{
    TempTree tree;
    write(tree, "secret.txt", "the contents");
    tree.makeDir("out");

    QString error;
    QVERIFY(engineCompressLocked({ tree.filePath("secret.txt") },
                                 tree.filePath("locked.zip"),
                                 QStringLiteral("hunter2"), &error));

    // No password: the failure must identify itself as a passphrase problem
    // (that is what turns into a prompt instead of an error), and staging
    // must leave nothing behind.
    QString produced;
    bool needsPassphrase = false;
    QVERIFY(!engineExtractLocked(tree.filePath("locked.zip"), tree.filePath("out"),
                                 QString(), &produced, &error, &needsPassphrase));
    QVERIFY2(needsPassphrase, qPrintable(error));
    QVERIFY(QDir(tree.filePath("out"))
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
}

void TestArchives::wrongPasswordAsksAgainNotErrors()
{
    TempTree tree;
    write(tree, "secret.txt", "the contents");
    tree.makeDir("out");

    QString error;
    QVERIFY(engineCompressLocked({ tree.filePath("secret.txt") },
                                 tree.filePath("locked.zip"),
                                 QStringLiteral("hunter2"), &error));

    QString produced;
    bool needsPassphrase = false;
    QVERIFY(!engineExtractLocked(tree.filePath("locked.zip"), tree.filePath("out"),
                                 QStringLiteral("wrong"), &produced, &error,
                                 &needsPassphrase));
    QVERIFY2(needsPassphrase, qPrintable(error));
}

void TestArchives::bsdtarCanReadOurEncryptedZip()
{
    if (QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())
        QSKIP("bsdtar not installed");

    TempTree tree;
    write(tree, "a.txt", "aaa");
    QString error;
    QVERIFY(engineCompressLocked({ tree.filePath("a.txt") }, tree.filePath("x.zip"),
                                 QStringLiteral("pw"), &error));

    // Second opinion: a foreign reader, given the passphrase, must get the
    // bytes back — a symmetric write/read bug would hide otherwise.
    tree.makeDir("foreign");
    QProcess tar;
    tar.setWorkingDirectory(tree.filePath("foreign"));
    tar.start(QStringLiteral("bsdtar"),
              { QStringLiteral("--passphrase"), QStringLiteral("pw"),
                QStringLiteral("-xf"), tree.filePath("x.zip") });
    QVERIFY(tar.waitForFinished(5000));
    QCOMPARE(tar.exitCode(), 0);
    QCOMPARE(read(tree.filePath("foreign/a.txt")), QStringLiteral("aaa"));
}

void TestArchives::archiveExtensionOffsets()
{
    QCOMPARE(ArchiveEngine::archiveExtensionOffset("photos.tar.gz"), 6);
    QCOMPARE(ArchiveEngine::archiveExtensionOffset("photos.zip"), 6);
    QCOMPARE(ArchiveEngine::archiveExtensionOffset("notes.txt.gz"), 9);
    QCOMPARE(ArchiveEngine::archiveExtensionOffset("plain.txt"), -1);
    QCOMPARE(ArchiveEngine::archiveExtensionOffset(".zip"), -1);
}

void TestArchives::archiveContentTypes()
{
    QVERIFY(ArchiveEngine::isArchiveContentType(QStringLiteral("application/zip")));
    QVERIFY(ArchiveEngine::isArchiveContentType(QStringLiteral("application/x-compressed-tar")));
    QVERIFY(!ArchiveEngine::isArchiveContentType(QStringLiteral("text/plain")));
    QVERIFY(!ArchiveEngine::isArchiveContentType(QString()));
}

void TestArchives::compressAndUndoRemovesTheArchive()
{
    TempTree tree;
    write(tree, "a.txt", "aaa");
    FileOperations ops;

    ops.compress({ tree.filePath("a.txt") }, tree.filePath("a.zip"));
    QVERIFY(settle(ops));
    QVERIFY(ops.lastError().isEmpty());
    QVERIFY(QFileInfo::exists(tree.filePath("a.zip")));
    QVERIFY(ops.canUndo());

    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(!QFileInfo::exists(tree.filePath("a.zip")));
    // The sources are untouched by the undo.
    QCOMPARE(read(tree.filePath("a.txt")), QStringLiteral("aaa"));
    QVERIFY(!ops.canUndo());
}

void TestArchives::extractAndUndoRemovesTheOutput()
{
    TempTree tree;
    write(tree, "stuff/x.txt", "x");
    write(tree, "stuff/y.txt", "y");
    QString error;
    QVERIFY(engineCompress({ tree.filePath("stuff") }, tree.filePath("stuff.zip"), &error));

    FileOperations ops;
    ops.extractHere({ tree.filePath("stuff.zip") }, tree.path());
    QVERIFY(settle(ops));
    QVERIFY(ops.lastError().isEmpty());
    // "stuff" exists, so the single top-level entry landed as "stuff 2".
    QVERIFY(QFileInfo::exists(tree.filePath("stuff 2/x.txt")));
    QVERIFY(ops.canUndo());

    ops.undo();
    QVERIFY(settle(ops));
    QVERIFY(!QFileInfo::exists(tree.filePath("stuff 2")));
    // The archive and the originals survive the undo.
    QVERIFY(QFileInfo::exists(tree.filePath("stuff.zip")));
    QCOMPARE(read(tree.filePath("stuff/x.txt")), QStringLiteral("x"));
}

QTEST_GUILESS_MAIN(TestArchives)
#include "tst_archives.moc"
