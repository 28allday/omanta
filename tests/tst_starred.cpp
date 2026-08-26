#include "StarredModel.h"
#include "StarredStore.h"
#include "DirectoryModel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Starring — the store's file format and rules, and the starred:/// listing.
class TestStarred : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void starsAndPersists();
    void unstarRemovesOnlyItsPath();
    void refusesNonLocalPaths();
    void allStarredAnswersForTheWholeSelection();
    void modelListsStarredEntries();
    void modelSkipsVanishedFilesButKeepsTheStar();
    void modelFollowsTheStore();

private:
    QString filePath(const QString &name) const
    {
        return QDir(m_files.path()).filePath(name);
    }
    void touch(const QString &name)
    {
        QFile file(filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    static bool waitForCount(StarredModel &model, int count, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (model.count() != count && timer.elapsed() < timeoutMs)
            QTest::qWait(20);
        return model.count() == count;
    }

    QTemporaryDir m_dir;   // the store file lives here
    QTemporaryDir m_files; // starred fixtures
};

void TestStarred::initTestCase()
{
    qputenv("OMANTA_STARRED_FILE",
            QDir(m_dir.path()).filePath(QStringLiteral("starred")).toUtf8());
}

void TestStarred::starsAndPersists()
{
    touch("kept.txt");
    {
        StarredStore store;
        store.star({ filePath("kept.txt") });
        QVERIFY(store.isStarred(filePath("kept.txt")));
    }
    // A fresh store reads the same file — stars survive the process.
    StarredStore store;
    QVERIFY(store.isStarred(filePath("kept.txt")));
    store.unstar({ filePath("kept.txt") });
}

void TestStarred::unstarRemovesOnlyItsPath()
{
    touch("one.txt");
    touch("two.txt");
    StarredStore store;
    store.star({ filePath("one.txt"), filePath("two.txt") });

    store.unstar({ filePath("one.txt") });
    QVERIFY(!store.isStarred(filePath("one.txt")));
    QVERIFY(store.isStarred(filePath("two.txt")));
    store.unstar({ filePath("two.txt") });
}

void TestStarred::refusesNonLocalPaths()
{
    StarredStore store;
    store.star({ QStringLiteral("trash:///gone.txt"), QStringLiteral("smb://host/share/x") });
    QCOMPARE(store.paths(), QStringList());
}

void TestStarred::allStarredAnswersForTheWholeSelection()
{
    touch("a.txt");
    touch("b.txt");
    StarredStore store;
    store.star({ filePath("a.txt") });

    QVERIFY(store.allStarred({ filePath("a.txt") }));
    QVERIFY(!store.allStarred({ filePath("a.txt"), filePath("b.txt") }));
    QVERIFY(!store.allStarred({}));
    store.unstar({ filePath("a.txt") });
}

void TestStarred::modelListsStarredEntries()
{
    touch("starred-one.txt");
    QDir(m_files.path()).mkdir(QStringLiteral("starred-dir"));

    StarredStore store;
    store.star({ filePath("starred-one.txt"), filePath("starred-dir") });

    StarredModel model;
    model.setStore(&store);
    model.setActive(true);
    QVERIFY(waitForCount(model, 2));

    // Name role is the FULL path (unique across folders); display is the
    // basename; filePath points at the real file.
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, DirectoryModel::NameRole).toString(),
             filePath("starred-one.txt"));
    QCOMPARE(model.data(first, DirectoryModel::DisplayNameRole).toString(),
             QStringLiteral("starred-one.txt"));
    QCOMPARE(model.data(model.index(1, 0), DirectoryModel::IsDirRole).toBool(), true);

    store.unstar({ filePath("starred-one.txt"), filePath("starred-dir") });
}

void TestStarred::modelSkipsVanishedFilesButKeepsTheStar()
{
    touch("here.txt");
    StarredStore store;
    store.star({ filePath("here.txt"), filePath("never-existed.txt") });

    StarredModel model;
    model.setStore(&store);
    model.setActive(true);
    QVERIFY(waitForCount(model, 1));

    // The listing hides the missing file; the star itself is not forgotten.
    QVERIFY(store.isStarred(filePath("never-existed.txt")));
    store.unstar({ filePath("here.txt"), filePath("never-existed.txt") });
}

void TestStarred::modelFollowsTheStore()
{
    touch("late.txt");
    StarredStore store;
    StarredModel model;
    model.setStore(&store);
    model.setActive(true);
    QVERIFY(waitForCount(model, 0));

    store.star({ filePath("late.txt") });
    QVERIFY(waitForCount(model, 1));

    store.unstar({ filePath("late.txt") });
    QVERIFY(waitForCount(model, 0));
}

QTEST_GUILESS_MAIN(TestStarred)
#include "tst_starred.moc"
