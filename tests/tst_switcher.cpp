#include "DefaultFileManager.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QProcess>
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
    void installsIntoAHandWrittenMenu();
    void leavesOtherDesktopsAlone();
    void switchesTheDefaultBothWays();
    void activationFallsBackWhenOmantaIsGone();
    void unavailableWithoutTheScript();
    void switchesTheFileChooserBothWays();
    void keepsAPortalsConfOfYourOwn();
    void leavesTheFileChooserWithoutThePortalFile();

private:
    static bool settle(DefaultFileManager &manager);
    QString menuFile() const { return m_home->filePath("config/omarchy/extensions/omarchy-menu.jsonc"); }
    QString bindingsFile() const { return m_home->filePath("config/hypr/bindings.lua"); }
    QString portalFile() const { return m_home->filePath("omanta.portal"); }
    QString portalsConf() const { return m_home->filePath("config/xdg-desktop-portal/omantatest-portals.conf"); }
    QByteArray readAll(const QString &path) const
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    QString serviceFile() const { return m_home->filePath("data/dbus-1/services/org.freedesktop.FileManager1.service"); }
    // Stubs on PATH; the quote and space prove the Exec= line's quoting.
    QString stubDir() const { return m_home->filePath("bin it's"); }
    QString stubLog() const { return m_home->filePath("stub.log"); }
    QByteArray runActivation(const QByteArray &activation) const;

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
    // The D-Bus activation file names the omanta binary; the script looks
    // beside itself, then on PATH, and a dev checkout has it in neither.
    // nautilus, busctl, systemctl and hyprctl are stubbed too, so neither the
    // fallback, the bus reload, the portal restart nor the compositor reload
    // can reach the desktop running the tests.
    QVERIFY(QDir().mkpath(stubDir()));
    for (const char *name : { "omanta", "nautilus", "busctl", "systemctl", "hyprctl" }) {
        QFile stub(stubDir() + QLatin1Char('/') + QLatin1String(name));
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write("#!/bin/sh\necho \"" + QByteArray(name) + " $*\" >> \"$STUB_LOG\"\n");
        stub.setPermissions(stub.permissions() | QFileDevice::ExeOwner);
    }
    qputenv("STUB_LOG", stubLog().toUtf8());
    qputenv("PATH", (stubDir() + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    qputenv("XDG_CONFIG_HOME", m_home->filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", m_home->filePath("data").toUtf8());
    qputenv("XDG_DATA_DIRS", m_home->filePath("data").toUtf8());
    qputenv("OMANTA_SETTINGS_FILE", m_home->filePath("settings").toUtf8());
    // The file chooser: a stand-in for the package's .portal file, and a
    // desktop with no system portals.conf of its own.
    {
        QFile portal(portalFile());
        QVERIFY(portal.open(QIODevice::WriteOnly));
        portal.write("[portal]\n");
    }
    qputenv("OMANTA_PORTAL_FILE", portalFile().toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "OmantaTest:Other");
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

void TestSwitcher::installsIntoAHandWrittenMenu()
{
    // A hand-written extension file rarely ends its last entry with a comma
    // (issue #26): the row must still go in, and the person's entries stay.
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy/extensions")));
    {
        QFile menu(menuFile());
        QVERIFY(menu.open(QIODevice::WriteOnly));
        menu.write("{\n  \"some.entry\": {\"label\":\"A\"},\n  \"other.entry\": {\"label\":\"B\"}\n}\n");
    }
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setMenuInstalled(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));

    // Installing again replaces the block rather than adding a second one.
    manager.setMenuInstalled(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));

    QFile menu(menuFile());
    QVERIFY(menu.open(QIODevice::ReadOnly));
    const QByteArray contents = menu.readAll();
    QCOMPARE(contents.count("omanta-switch toggle"), 1);
    QVERIFY(contents.contains("\"some.entry\": {\"label\":\"A\"},\n"));
    QVERIFY(contents.contains("\"other.entry\": {\"label\":\"B\"}\n}"));
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
    // "Show in folder" with no file manager running activates omanta.
    QFile service(serviceFile());
    QVERIFY(service.open(QIODevice::ReadOnly));
    const QByteArray activation = service.readAll();
    service.close();
    QVERIFY(activation.contains("Name=org.freedesktop.FileManager1\n"));
    QCOMPARE(runActivation(activation), QByteArray("omanta --service\n"));

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    QCOMPARE(bindings.readAll(), original); // restored byte for byte
    QVERIFY(!QFile::exists(serviceFile())); // Nautilus's own .service applies again
    QVERIFY(status.count() >= 2);
    QVERIFY(manager.lastError().isEmpty());
}

// Runs the Exec= line the way the bus would (shell word rules) and returns
// what the stubs logged.
QByteArray TestSwitcher::runActivation(const QByteArray &activation) const
{
    QFile::remove(stubLog());
    QByteArray exec;
    for (const QByteArray &line : activation.split('\n'))
        if (line.startsWith("Exec="))
            exec = line.mid(5);
    QProcess bus;
    bus.start(QStringLiteral("/bin/sh"),
              { QStringLiteral("-c"), QStringLiteral("eval \"set -- $1\"; exec \"$@\""),
                QStringLiteral("sh"), QString::fromUtf8(exec) });
    if (!bus.waitForFinished())
        return {};
    QFile log(stubLog());
    return log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
}

// Uninstalled without switching back: the activation file must not keep
// shadowing Nautilus with a binary that is gone.
void TestSwitcher::activationFallsBackWhenOmantaIsGone()
{
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QFile service(serviceFile());
    QVERIFY(service.open(QIODevice::ReadOnly));
    const QByteArray activation = service.readAll();
    service.close();

    QVERIFY(QFile::remove(stubDir() + QLatin1String("/omanta")));
    const QByteArray ran = runActivation(activation);
    QVERIFY(!QFile::exists(serviceFile()));
    QVERIFY2(ran.contains("busctl --user call org.freedesktop.DBus"), ran.constData());
    QVERIFY2(ran.endsWith("nautilus --gapplication-service\n"), ran.constData());
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

// Open and Save dialogs follow the switch: portals.conf for this desktop
// names omanta's backend, the portal is restarted to read it, and the picker
// window floats. With no portals.conf of the person's own, the file is ours
// and goes again on the way back.
void TestSwitcher::switchesTheFileChooserBothWays()
{
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    QFile::remove(stubLog());
    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));

    const QByteArray conf = readAll(portalsConf());
    QVERIFY2(conf.contains("[preferred]\n"), conf.constData());
    QVERIFY2(conf.contains("\norg.freedesktop.impl.portal.FileChooser=omanta\n"), conf.constData());
    // No system file for this desktop to start from: everything else stays GTK.
    QVERIFY2(conf.contains("\ndefault=gtk\n"), conf.constData());
    QVERIFY(readAll(stubLog()).contains("systemctl --user try-restart xdg-desktop-portal.service"));
    QVERIFY(readAll(bindingsFile()).contains("o.window(\"^omanta-picker$\""));

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QVERIFY(!QFile::exists(portalsConf()));
    QVERIFY(!readAll(bindingsFile()).contains("omanta-picker"));
}

void TestSwitcher::keepsAPortalsConfOfYourOwn()
{
    QVERIFY(QDir().mkpath(m_home->filePath("config/xdg-desktop-portal")));
    const QByteArray original = "# mine\n[preferred]\ndefault=hyprland;gtk\n"
                                "org.freedesktop.impl.portal.Settings=darkman\n";
    {
        QFile conf(portalsConf());
        QVERIFY(conf.open(QIODevice::WriteOnly));
        conf.write(original);
    }
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setDefault(true);
    QVERIFY(settle(manager));
    // Switching on twice must not stack a second line.
    manager.setDefault(true);
    QVERIFY(settle(manager));
    const QByteArray conf = readAll(portalsConf());
    QCOMPARE(conf.count("FileChooser=omanta"), 1);
    QVERIFY(conf.startsWith("# mine\n[preferred]\n"));
    QVERIFY(conf.contains("default=hyprland;gtk\n"));
    QVERIFY(conf.contains("org.freedesktop.impl.portal.Settings=darkman\n"));

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QCOMPARE(readAll(portalsConf()), original); // restored byte for byte
}

void TestSwitcher::leavesTheFileChooserWithoutThePortalFile()
{
    // A user-local dev install has no .portal file where the frontend looks;
    // pointing portals.conf at a backend it cannot find would leave every
    // app without a file chooser at all.
    QVERIFY(QFile::remove(portalFile()));
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QVERIFY(!QFile::exists(portalsConf()));
}

QTEST_MAIN(TestSwitcher)
#include "tst_switcher.moc"
