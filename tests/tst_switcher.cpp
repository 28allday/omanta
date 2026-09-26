#include "DefaultFileManager.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

// DefaultFileManager against the real omanta-switch script, in a throwaway
// XDG config/data home: nothing here may touch the desktop running the tests.
class TestSwitcher : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void offersTheToggleMenuOnceOnOmarchy();
    void leavesOtherDesktopsAlone();
    void switchesTheDefaultBothWays();
    void unavailableWithoutTheScript();

private:
    static bool settle(DefaultFileManager &manager);
    QString menuFile() const { return m_home->filePath("config/omarchy/extensions/omarchy-menu.jsonc"); }
    QString bindingsFile() const { return m_home->filePath("config/hypr/bindings.lua"); }

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestSwitcher::initTestCase()
{
    if (QStandardPaths::findExecutable("xdg-mime").isEmpty()
        || QStandardPaths::findExecutable("perl").isEmpty())
        QSKIP("omanta-switch needs xdg-mime and perl");
    // No Hyprland: the script would otherwise reload the live compositor.
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    qputenv("OMANTA_SWITCH", OMANTA_SWITCH_SCRIPT);
}

void TestSwitcher::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    QVERIFY(QDir().mkpath(m_home->filePath("config")));
    QVERIFY(QDir().mkpath(m_home->filePath("data/applications")));
    // The real entry's MimeType list, but an Exec that resolves anywhere:
    // xdg-mime ignores an entry whose binary is not on PATH.
    {
        QFile source(OMANTA_DESKTOP_FILE);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray entry = source.readAll();
        QStringList lines = QString::fromUtf8(entry).split(QLatin1Char('\n'));
        for (QString &line : lines) {
            if (line.startsWith(QLatin1String("Exec=")))
                line = QStringLiteral("Exec=/bin/sh %U");
        }
        QFile copy(m_home->filePath("data/applications/omanta.desktop"));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(lines.join(QLatin1Char('\n')).toUtf8());
    }
    // Stock Omarchy pins Nautilus for folders. Without that, xdg-mime falls
    // back to scanning desktop files and omanta, the only one here, wins.
    {
        QFile mimeapps(m_home->filePath("config/mimeapps.list"));
        QVERIFY(mimeapps.open(QIODevice::WriteOnly));
        mimeapps.write("[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n");
    }
    // xdg-mime skips a default whose desktop file is not installed.
    {
        QFile nautilus(m_home->filePath("data/applications/org.gnome.Nautilus.desktop"));
        QVERIFY(nautilus.open(QIODevice::WriteOnly));
        nautilus.write("[Desktop Entry]\nType=Application\nName=Files\nExec=/bin/sh\n"
                       "MimeType=inode/directory;\n");
    }
    qputenv("XDG_CONFIG_HOME", m_home->filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", m_home->filePath("data").toUtf8());
    qputenv("XDG_DATA_DIRS", m_home->filePath("data").toUtf8());
    qputenv("OMANTA_SETTINGS_FILE", m_home->filePath("settings").toUtf8());
}

void TestSwitcher::cleanup()
{
    m_home.reset();
}

bool TestSwitcher::settle(DefaultFileManager &manager)
{
    // Commands queue; wait for the last to finish and its status to land.
    return QTest::qWaitFor([&] { return !manager.busy() && manager.known(); }, 15000);
}

void TestSwitcher::offersTheToggleMenuOnceOnOmarchy()
{
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy")));
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(manager.available());
    QVERIFY(manager.omarchy());
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());

    manager.offerToggleMenu(&settings);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));
    QVERIFY(settings.toggleMenuOffered());
    QFile menu(menuFile());
    QVERIFY(menu.open(QIODevice::ReadOnly));
    QVERIFY(menu.readAll().contains("omanta-switch toggle"));
    menu.close();

    // Offering only adds the row; the default is still the person's call.
    QVERIFY(!manager.isDefault());
    QVERIFY(!QFile::exists(bindingsFile()));

    // Removed by hand (or from Preferences): a later launch leaves it gone.
    manager.setMenuInstalled(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());
    Settings reread;
    DefaultFileManager relaunched;
    QVERIFY(settle(relaunched));
    relaunched.offerToggleMenu(&reread);
    QVERIFY(settle(relaunched));
    QVERIFY(!relaunched.menuInstalled());
}

void TestSwitcher::leavesOtherDesktopsAlone()
{
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(!manager.omarchy());
    manager.offerToggleMenu(&settings);
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());
    QVERIFY(!QFile::exists(menuFile()));
    QVERIFY(!settings.toggleMenuOffered());
}

void TestSwitcher::switchesTheDefaultBothWays()
{
    QVERIFY(QDir().mkpath(m_home->filePath("config/hypr")));
    const QByteArray original = "-- user bindings\no.bind(\"SUPER + X\", \"Thing\", \"thing\")\n";
    {
        QFile bindings(bindingsFile());
        QVERIFY(bindings.open(QIODevice::WriteOnly));
        bindings.write(original);
    }
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QSignalSpy status(&manager, &DefaultFileManager::statusChanged);

    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QFile bindings(bindingsFile());
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    QVERIFY(bindings.readAll().contains("omanta-launch"));
    bindings.close();

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    QCOMPARE(bindings.readAll(), original); // restored byte for byte
    QVERIFY(status.count() >= 2);
    QVERIFY(manager.lastError().isEmpty());
}

void TestSwitcher::unavailableWithoutTheScript()
{
    qputenv("OMANTA_SWITCH", "/nonexistent/omanta-switch");
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy")));
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(!manager.available());
    manager.offerToggleMenu(&settings);
    manager.setDefault(true);
    QVERIFY(!manager.busy());
    QVERIFY(!settings.toggleMenuOffered());
    QVERIFY(!QFile::exists(menuFile()));
    qputenv("OMANTA_SWITCH", OMANTA_SWITCH_SCRIPT);
}

QTEST_MAIN(TestSwitcher)
#include "tst_switcher.moc"
