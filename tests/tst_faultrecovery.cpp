#include "ArchiveEngine.h"
#include "FileOperations.h"
#include "FileOperationWorker.h"
#include "TestFixture.h"

#include <QCryptographicHash>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class TestFaultRecovery : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void transferFailure_data();
    void transferFailure();
};

static QByteArray digest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

void TestFaultRecovery::transferFailure_data()
{
    QTest::addColumn<bool>("move");
    QTest::addColumn<bool>("replace");
    QTest::addColumn<bool>("readOnly");
    for (bool move : {false, true}) {
        for (bool replace : {false, true}) {
            for (bool readOnly : {false, true}) {
                const QByteArray name = QByteArray(move ? "move" : "copy")
                    + (replace ? "-replace" : "-new") + (readOnly ? "-readonly" : "-full");
                QTest::newRow(name.constData()) << move << replace << readOnly;
            }
        }
    }
}

void TestFaultRecovery::transferFailure()
{
    if (qEnvironmentVariable("OMANTA_TEST_FAULT_SANDBOX") != "1") {
        if (QStandardPaths::findExecutable("bwrap").isEmpty())
            QSKIP("Isolated full-disk tests require bubblewrap");
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start("bwrap", {
            "--unshare-all", "--die-with-parent",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
            "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
            "--symlink", "usr/lib", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/home/test",
            "--size", "2097152", "--tmpfs", "/limited",
            "--setenv", "HOME", "/home/test", "--setenv", "GIO_USE_VFS", "local",
            "--setenv", "OMANTA_TEST_FAULT_SANDBOX", "1",
            "--ro-bind", QCoreApplication::applicationFilePath(), "/test",
            "--chdir", "/home/test", "/test",
            QString("transferFailure:%1").arg(QString::fromLatin1(QTest::currentDataTag()))
        });
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(30000));
        const QByteArray output = child.readAll();
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 output.constData());
        return;
    }
    QFETCH(bool, move);
    QFETCH(bool, replace);
    QFETCH(bool, readOnly);
    TempTree source;
    const QString original = source.writeFile("large", 4 * 1024 * 1024);
    const QByteArray originalHash = digest(original);
    QVERIFY(!originalHash.isEmpty());
    const QString target = "/limited/large";
    QByteArray previousHash;
    if (replace) {
        QFile previous(target);
        QVERIFY(previous.open(QIODevice::WriteOnly));
        QCOMPARE(previous.write("existing destination data"), 25);
        previous.close();
        previousHash = digest(target);
    }
    if (readOnly)
        QVERIFY(QFile::setPermissions("/limited", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    FileOperationWorker worker;
    QSignalSpy failed(&worker, &FileOperationWorker::failed);
    QSignalSpy succeeded(&worker, &FileOperationWorker::succeeded);
    FileOperationRequest request;
    request.kind = move ? FileOperationRequest::Move : FileOperationRequest::Copy;
    request.sources = {original};
    request.destination = "/limited";
    request.policy = replace ? ConflictPolicy::Replace : ConflictPolicy::RenameNew;
    worker.run(request, 1);
    QCOMPARE(succeeded.size(), 0);
    QCOMPARE(failed.size(), 1);
    QVERIFY(!failed.first().at(1).toString().isEmpty());
    QCOMPARE(digest(original), originalHash);
    if (replace)
        QCOMPARE(digest(target), previousHash);
    else
        QVERIFY2(!QFileInfo::exists(target), "A failed copy must not leave a truncated file at its final name");
    QCOMPARE(QDir("/limited").entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             replace ? QStringList{"large"} : QStringList{});
}

QTEST_GUILESS_MAIN(TestFaultRecovery)
#include "tst_faultrecovery.moc"
