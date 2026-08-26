#include "NetworkModel.h"
#include "ServerStore.h"
#include "DirectoryModel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

// Known network servers — the store's file format and rules, and the
// network:/// listing. The model's assertions are containment-based, not
// exact counts: on a desktop session the model legitimately also sees real
// mounted shares and gvfs discovery, and the tests must not fail because the
// machine has a network.
class TestServers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void addsAndPersists();
    void normalizesTrailingSlash();
    void refusesNonServerAddresses();
    void removeDropsOnlyItsUri();
    void allKnownAnswersForTheWholeSelection();
    void modelListsKnownServers();
    void modelPrettyPrintsAddresses();
    void modelFollowsTheStore();

private:
    QString storeFile() const
    {
        return QDir(m_dir.path()).filePath(QStringLiteral("servers"));
    }
    void resetStoreFile(const QByteArray &content = {})
    {
        QFile file(storeFile());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }
    static bool waitFor(const std::function<bool()> &done, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < timeoutMs)
            QTest::qWait(20);
        return done();
    }
    static QStringList modelUris(const NetworkModel &model)
    {
        QStringList uris;
        for (int row = 0; row < model.count(); ++row)
            uris.append(model.data(model.index(row, 0), DirectoryModel::NameRole).toString());
        return uris;
    }
    static QString displayNameFor(const NetworkModel &model, const QString &uri)
    {
        for (int row = 0; row < model.count(); ++row) {
            const QModelIndex idx = model.index(row, 0);
            if (model.data(idx, DirectoryModel::NameRole).toString() == uri)
                return model.data(idx, DirectoryModel::DisplayNameRole).toString();
        }
        return {};
    }

    QTemporaryDir m_dir; // the store file lives here
};

void TestServers::initTestCase()
{
    qputenv("OMANTA_SERVERS_FILE", storeFile().toUtf8());
}

void TestServers::addsAndPersists()
{
    resetStoreFile();
    {
        ServerStore store;
        store.add(QStringLiteral("smb://demo.local/media"));
        QVERIFY(store.isKnown(QStringLiteral("smb://demo.local/media")));
    }
    // A fresh store reads the same file — connections survive the process.
    ServerStore store;
    QVERIFY(store.isKnown(QStringLiteral("smb://demo.local/media")));
    QCOMPARE(store.uris(), QStringList{ QStringLiteral("smb://demo.local/media") });
}

void TestServers::normalizesTrailingSlash()
{
    resetStoreFile();
    ServerStore store;
    store.add(QStringLiteral("sftp://durden/data/"));
    // The typed form and the mount's reported form are the same server.
    QVERIFY(store.isKnown(QStringLiteral("sftp://durden/data")));
    QVERIFY(store.isKnown(QStringLiteral("sftp://durden/data/")));
    store.add(QStringLiteral("sftp://durden/data"));
    QCOMPARE(store.uris().size(), 1);
}

void TestServers::refusesNonServerAddresses()
{
    resetStoreFile();
    ServerStore store;
    store.add(QStringLiteral("/home/nobody/notaserver"));
    store.add(QStringLiteral("file:///home/nobody"));
    store.add(QStringLiteral("just-a-hostname"));
    store.add(QString());
    QCOMPARE(store.uris(), QStringList{});
}

void TestServers::removeDropsOnlyItsUri()
{
    resetStoreFile();
    ServerStore store;
    store.add(QStringLiteral("smb://one/share"));
    store.add(QStringLiteral("smb://two/share"));
    store.remove({ QStringLiteral("smb://one/share") });
    QVERIFY(!store.isKnown(QStringLiteral("smb://one/share")));
    QVERIFY(store.isKnown(QStringLiteral("smb://two/share")));
}

void TestServers::allKnownAnswersForTheWholeSelection()
{
    resetStoreFile();
    ServerStore store;
    store.add(QStringLiteral("smb://one/share"));
    QVERIFY(store.allKnown({ QStringLiteral("smb://one/share") }));
    QVERIFY(!store.allKnown({ QStringLiteral("smb://one/share"),
                              QStringLiteral("smb://absent/share") }));
    QVERIFY(!store.allKnown({}));
}

void TestServers::modelListsKnownServers()
{
    resetStoreFile("smb://demo.local/media\nsftp://durden/data\n");
    ServerStore store;
    NetworkModel model;
    model.setStore(&store);
    model.setActive(true);

    QVERIFY(waitFor([&] {
        const QStringList uris = modelUris(model);
        return uris.contains(QStringLiteral("smb://demo.local/media"))
            && uris.contains(QStringLiteral("sftp://durden/data"));
    }));

    // Every row speaks DirectoryModel's roles and opens as a place.
    const QModelIndex first = model.index(0, 0);
    QVERIFY(model.data(first, DirectoryModel::IsDirRole).toBool());
    QCOMPARE(model.data(first, DirectoryModel::ContentTypeRole).toString(),
             QStringLiteral("inode/directory"));
}

void TestServers::modelPrettyPrintsAddresses()
{
    resetStoreFile("smb://demo.local/media\nsftp://durden\n");
    ServerStore store;
    NetworkModel model;
    model.setStore(&store);
    model.setActive(true);

    QVERIFY(waitFor([&] { return model.count() >= 2; }));
    // "share on host" for an address with a path; a bare host stands alone.
    QCOMPARE(displayNameFor(model, QStringLiteral("smb://demo.local/media")),
             QStringLiteral("media on demo.local"));
    QCOMPARE(displayNameFor(model, QStringLiteral("sftp://durden")),
             QStringLiteral("durden"));
}

void TestServers::modelFollowsTheStore()
{
    resetStoreFile();
    ServerStore store;
    NetworkModel model;
    model.setStore(&store);
    model.setActive(true);

    store.add(QStringLiteral("smb://late/arrival"));
    QVERIFY(waitFor([&] {
        return modelUris(model).contains(QStringLiteral("smb://late/arrival"));
    }));

    store.remove({ QStringLiteral("smb://late/arrival") });
    QVERIFY(waitFor([&] {
        return !modelUris(model).contains(QStringLiteral("smb://late/arrival"));
    }));
}

QTEST_GUILESS_MAIN(TestServers)
#include "tst_servers.moc"
