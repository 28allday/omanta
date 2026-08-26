#include "UserActions.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// The declarative context actions — the replacement for the nautilus-python
// extensions. What matters: eligibility filtering (mime, extension,
// directories), requires-hiding, labels, and the exact processes run()
// would start — asserted via invocationsFor(), not by launching anything.
class TestUserActions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void loadsActionsAndFiltersByAvailability();
    void filtersByMimetypeAndExtension();
    void skipsDirectoriesWhenAsked();
    void underLimitsToListedRoots();
    void labelsUsePluralWithCount();
    void substitutesAllPathsForF();
    void eachRunsOncePerPath();
    void terminalActionsWrapIntoOneShellCommand();
    void userDirOverridesById();
    void reloadsWhenTheDirChanges();

private:
    void writeAction(const QString &name, const QString &body);
    QString touchExecutable(const QString &name);

    // The actions dir is shared across the test functions, so "no actions at
    // all" is never the right assertion — ask about the one under test.
    static bool offers(const UserActions &actions, const QStringList &paths,
                       const QString &id)
    {
        const QVariantList list = actions.actionsFor(paths);
        for (const QVariant &entry : list) {
            if (entry.toMap().value("id").toString() == id)
                return true;
        }
        return false;
    }

    QTemporaryDir m_dir;      // the actions dir
    QTemporaryDir m_bin;      // fake executables, prepended to PATH
    QTemporaryDir m_files;    // selection fixtures
    QString m_image, m_video, m_text, m_folder;
};

void TestUserActions::writeAction(const QString &name, const QString &body)
{
    QFile file(QDir(m_dir.path()).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(body.toUtf8());
}

QString TestUserActions::touchExecutable(const QString &name)
{
    const QString path = QDir(m_bin.path()).filePath(name);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    file.write("#!/bin/sh\n");
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return path;
}

void TestUserActions::initTestCase()
{
    qputenv("OMANTA_ACTIONS_DIR", m_dir.path().toUtf8());
    qputenv("PATH", (m_bin.path() + QLatin1Char(':')
                     + qEnvironmentVariable("PATH")).toUtf8());

    const QDir files(m_files.path());
    auto touch = [&](const QString &name) {
        QFile f(files.filePath(name));
        const bool opened = f.open(QIODevice::WriteOnly);
        Q_ASSERT(opened);
        Q_UNUSED(opened);
        f.write("x");
        return files.filePath(name);
    };
    m_image = touch("photo.jpg");
    m_video = touch("clip.mp4");
    m_text = touch("notes.txt");
    files.mkdir("folder");
    m_folder = files.filePath("folder");
}

void TestUserActions::loadsActionsAndFiltersByAvailability()
{
    touchExecutable("present-tool");
    writeAction("present.toml",
                "name = \"Present\"\ncommand = \"present-tool %F\"\n"
                "requires = [\"present-tool\"]\n");
    writeAction("absent.toml",
                "name = \"Absent\"\ncommand = \"no-such-tool-xyz %F\"\n"
                "requires = [\"no-such-tool-xyz\"]\n");
    UserActions actions;

    const QVariantList list = actions.actionsFor({ m_text });
    QStringList labels;
    for (const QVariant &entry : list)
        labels.append(entry.toMap().value("label").toString());

    QVERIFY(labels.contains(QStringLiteral("Present")));
    QVERIFY2(!labels.contains(QStringLiteral("Absent")),
             "an action whose tool is missing must hide, as the extensions did");
}

void TestUserActions::filtersByMimetypeAndExtension()
{
    touchExecutable("convert-tool");
    writeAction("convert.toml",
                "name = \"Convert\"\ncommand = \"convert-tool %F\"\n"
                "requires = [\"convert-tool\"]\n"
                "mimetypes = [\"image/*\", \"video/*\"]\n"
                "extensions = [\".heic\"]\n");
    UserActions actions;

    QVERIFY(offers(actions, { m_image }, QStringLiteral("convert")));
    QVERIFY(offers(actions, { m_video }, QStringLiteral("convert")));
    QVERIFY(!offers(actions, { m_text }, QStringLiteral("convert")));

    // The extension list catches what name-based mime detection misses.
    QFile heic(QDir(m_files.path()).filePath("shot.HEIC"));
    QVERIFY(heic.open(QIODevice::WriteOnly));
    heic.write("x");
    heic.close();
    QVERIFY(offers(actions, { heic.fileName() }, QStringLiteral("convert")));

    // A mixed selection is eligible through its matching subset only.
    const QList<QStringList> argvs =
        actions.invocationsFor(QStringLiteral("convert"), { m_text, m_image });
    QCOMPARE(argvs.size(), 1);
    QVERIFY(argvs.first().contains(m_image));
    QVERIFY(!argvs.first().contains(m_text));
}

void TestUserActions::skipsDirectoriesWhenAsked()
{
    touchExecutable("files-only-tool");
    writeAction("filesonly.toml",
                "name = \"Files Only\"\ncommand = \"files-only-tool %F\"\n"
                "requires = [\"files-only-tool\"]\ndirectories = false\n");
    UserActions actions;

    QVERIFY(!offers(actions, { m_folder }, QStringLiteral("filesonly")));
    const QList<QStringList> argvs =
        actions.invocationsFor(QStringLiteral("filesonly"), { m_folder, m_text });
    QCOMPARE(argvs.size(), 1);
    QVERIFY(!argvs.first().contains(m_folder));
}

// `under` scopes an action to chosen folders — the Dropbox-link actions
// must not appear on files outside ~/Dropbox.
void TestUserActions::underLimitsToListedRoots()
{
    writeAction("scoped.toml",
                QString::fromLatin1("name = \"Scoped\"\ncommand = \"true %f\"\neach = true\n"
                                    "under = [\"%1\"]\n").arg(m_files.path()));
    UserActions actions;

    auto labels = [&](const QString &path) {
        QStringList out;
        for (const QVariant &entry : actions.actionsFor({ path }))
            out.append(entry.toMap().value("label").toString());
        return out;
    };

    // Inside the root: eligible. Outside it: gone (other actions from this
    // suite may still match — assert on this action's label alone).
    QVERIFY(labels(m_text).contains(QStringLiteral("Scoped")));
    QTemporaryDir outside;
    QFile f(QDir(outside.path()).filePath("out.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    QVERIFY(!labels(f.fileName()).contains(QStringLiteral("Scoped")));
}

void TestUserActions::labelsUsePluralWithCount()
{
    touchExecutable("send-tool");
    writeAction("send.toml",
                "name = \"Send it\"\nplural = \"Send %n items\"\n"
                "command = \"send-tool %F\"\nrequires = [\"send-tool\"]\n");
    UserActions actions;

    auto labelFor = [&](const QStringList &paths) {
        const QVariantList list = actions.actionsFor(paths);
        for (const QVariant &entry : list) {
            if (entry.toMap().value("id").toString() == QLatin1String("send"))
                return entry.toMap().value("label").toString();
        }
        return QString();
    };

    QCOMPARE(labelFor({ m_text }), QStringLiteral("Send it"));
    QCOMPARE(labelFor({ m_text, m_image }), QStringLiteral("Send 2 items"));
}

void TestUserActions::substitutesAllPathsForF()
{
    const QString tool = touchExecutable("multi-tool");
    writeAction("multi.toml",
                "name = \"Multi\"\ncommand = \"multi-tool --flag %F\"\n"
                "requires = [\"multi-tool\"]\n");
    UserActions actions;

    const QList<QStringList> argvs =
        actions.invocationsFor(QStringLiteral("multi"), { m_text, m_image });
    QCOMPARE(argvs.size(), 1);
    QCOMPARE(argvs.first(),
             QStringList({ tool, QStringLiteral("--flag"), m_text, m_image }));
}

void TestUserActions::eachRunsOncePerPath()
{
    const QString tool = touchExecutable("single-tool");
    writeAction("single.toml",
                "name = \"Single\"\ncommand = \"single-tool %f\"\n"
                "requires = [\"single-tool\"]\neach = true\n");
    UserActions actions;

    const QList<QStringList> argvs =
        actions.invocationsFor(QStringLiteral("single"), { m_text, m_image });
    QCOMPARE(argvs.size(), 2);
    QCOMPARE(argvs.at(0), QStringList({ tool, m_text }));
    QCOMPARE(argvs.at(1), QStringList({ tool, m_image }));
}

void TestUserActions::terminalActionsWrapIntoOneShellCommand()
{
    const QString tool = touchExecutable("term-tool");
    const QString wrapper =
        touchExecutable("omarchy-launch-floating-terminal-with-presentation");
    writeAction("term.toml",
                "name = \"Terminal\"\ncommand = \"term-tool %f\"\n"
                "requires = [\"term-tool\"]\neach = true\nterminal = true\n");
    UserActions actions;

    const QList<QStringList> argvs =
        actions.invocationsFor(QStringLiteral("term"), { m_text, m_image });

    // However many files, terminal mode is ONE wrapper invocation whose
    // argument chains the per-file commands, failures skipped not fatal.
    QCOMPARE(argvs.size(), 1);
    QCOMPARE(argvs.first().size(), 2);
    QCOMPARE(argvs.first().first(), wrapper);
    const QString shell = argvs.first().at(1);
    QVERIFY(shell.contains(tool));
    QVERIFY(shell.contains(m_text));
    QVERIFY(shell.contains(m_image));
    QVERIFY(shell.contains(QStringLiteral("|| true")));
    QVERIFY(shell.contains(QStringLiteral("; ")));
}

void TestUserActions::userDirOverridesById()
{
    // With OMANTA_ACTIONS_DIR there is only one dir, so this exercises the
    // by-id override the two real dirs rely on: a later parse of the same id
    // replaces the earlier one.
    touchExecutable("override-tool");
    writeAction("override.toml",
                "name = \"Original\"\ncommand = \"override-tool %F\"\n"
                "requires = [\"override-tool\"]\n");
    UserActions actions;
    writeAction("override.toml",
                "name = \"Replaced\"\ncommand = \"override-tool %F\"\n"
                "requires = [\"override-tool\"]\n");
    actions.reload();

    const QVariantList list = actions.actionsFor({ m_text });
    QStringList labels;
    for (const QVariant &entry : list)
        labels.append(entry.toMap().value("label").toString());
    QVERIFY(labels.contains(QStringLiteral("Replaced")));
    QVERIFY(!labels.contains(QStringLiteral("Original")));
}

void TestUserActions::reloadsWhenTheDirChanges()
{
    touchExecutable("late-tool");
    UserActions actions;
    QSignalSpy spy(&actions, &UserActions::actionsChanged);

    writeAction("late.toml",
                "name = \"Late Arrival\"\ncommand = \"late-tool %F\"\n"
                "requires = [\"late-tool\"]\n");
    QVERIFY(spy.wait(5000));

    const QVariantList list = actions.actionsFor({ m_text });
    QStringList labels;
    for (const QVariant &entry : list)
        labels.append(entry.toMap().value("label").toString());
    QVERIFY(labels.contains(QStringLiteral("Late Arrival")));
}

QTEST_GUILESS_MAIN(TestUserActions)
#include "tst_actions.moc"
