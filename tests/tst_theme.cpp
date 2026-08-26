#include "SystemTheme.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// SystemTheme's Omarchy side: parsing colors.toml, deriving the roles the
// minimal format lacks, and following a theme switch by watching the file.
// The portal side stays untested here — it needs a session bus and a desktop.
class TestTheme : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesFlatToml();
    void parseIgnoresJunk();

    void missingFileMeansNoThemeColors();
    void minimalFormatDerivesEveryRole();
    void fullFormatUsesAuthoredRoles();
    void modeKeyBeatsLuminance();
    void themeSwitchIsPickedUpLive();

private:
    QString writeColors(const QString &content);
    QTemporaryDir m_dir;
};

QString TestTheme::writeColors(const QString &content)
{
    const QString path = m_dir.filePath(QStringLiteral("colors.toml"));
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    file.write(content.toUtf8());
    file.close();
    return path;
}

void TestTheme::parsesFlatToml()
{
    const auto values = SystemTheme::parseColorsToml(QStringLiteral(
        "mode = \"dark\"\n"
        "\n"
        "# a comment\n"
        "accent = \"#89b4fa\"\n"
        "background = \"#1e1e2e\"   # trailing comment\n"
        "color1 = \"#f38ba8\"\n"));

    QCOMPARE(values.value("mode"), QStringLiteral("dark"));
    QCOMPARE(values.value("accent"), QStringLiteral("#89b4fa"));
    QCOMPARE(values.value("background"), QStringLiteral("#1e1e2e"));
    QCOMPARE(values.value("color1"), QStringLiteral("#f38ba8"));
}

void TestTheme::parseIgnoresJunk()
{
    // The `#` inside quotes is the colour, not a comment — the exact thing
    // QSettings gets wrong and the reason this parser exists.
    const auto values = SystemTheme::parseColorsToml(QStringLiteral(
        "[section]\n"
        "= nothing\n"
        "no_value =\n"
        "unterminated = \"#aa\n"
        "fine = \"#abcdef\"\n"));

    QCOMPARE(values.size(), 1);
    QCOMPARE(values.value("fine"), QStringLiteral("#abcdef"));
}

void TestTheme::missingFileMeansNoThemeColors()
{
    qputenv("OMANTA_COLORS_FILE", (m_dir.filePath(QStringLiteral("nope.toml"))).toUtf8());
    SystemTheme theme;
    QVERIFY(!theme.hasThemeColors());
}

void TestTheme::minimalFormatDerivesEveryRole()
{
    // What omarchy-theme-set generates for an old-format theme: the anchors
    // and the terminal palette, nothing else. Everything must still resolve.
    qputenv("OMANTA_COLORS_FILE", writeColors(QStringLiteral(
        "accent = \"#69c3ff\"\n"
        "selection = \"#bcc1dc\"\n"
        "background = \"#111422\"\n"
        "foreground = \"#bcc1dc\"\n"
        "color1 = \"#e35535\"\n")).toUtf8());

    SystemTheme theme;
    QVERIFY(theme.hasThemeColors());
    QVERIFY(theme.darkMode()); // no mode key; judged by the background
    QCOMPARE(theme.windowColor(), QColor("#111422"));
    QCOMPARE(theme.textColor(), QColor("#bcc1dc"));
    QCOMPARE(theme.accentColor(), QColor("#69c3ff"));
    QCOMPARE(theme.selectionColor(), QColor("#bcc1dc"));
    QCOMPARE(theme.errorColor(), QColor("#e35535"));

    // Derived roles: valid, distinct from the anchors, and stepping from the
    // background toward the text rather than off into some third hue.
    for (const QColor &c : { theme.chromeColor(), theme.hoverColor(),
                             theme.borderColor(), theme.textDimColor() })
        QVERIFY(c.isValid());
    QVERIFY(theme.chromeColor() != theme.windowColor());
    QVERIFY(theme.hoverColor() != theme.windowColor());
    QVERIFY(theme.textDimColor() != theme.textColor());
    QVERIFY(theme.chromeColor().lightnessF() > theme.windowColor().lightnessF());
    QVERIFY(theme.textDimColor().lightnessF() < theme.textColor().lightnessF());
}

void TestTheme::fullFormatUsesAuthoredRoles()
{
    qputenv("OMANTA_COLORS_FILE", writeColors(QStringLiteral(
        "mode = \"dark\"\n"
        "accent = \"#89b4fa\"\n"
        "selection = \"#45475a\"\n"
        "background = \"#1e1e2e\"\n"
        "dark_background = \"#161622\"\n"
        "lighter_background = \"#313244\"\n"
        "foreground = \"#cdd6f4\"\n"
        "dark_foreground = \"#6c7086\"\n"
        "red = \"#f38ba8\"\n")).toUtf8());

    SystemTheme theme;
    QVERIFY(theme.hasThemeColors());
    QCOMPARE(theme.chromeColor(), QColor("#161622"));
    QCOMPARE(theme.hoverColor(), QColor("#313244"));
    QCOMPARE(theme.textDimColor(), QColor("#6c7086"));
    QCOMPARE(theme.errorColor(), QColor("#f38ba8"));
}

void TestTheme::modeKeyBeatsLuminance()
{
    // A light theme with a darkish background must still be treated as light
    // when it says so.
    qputenv("OMANTA_COLORS_FILE", writeColors(QStringLiteral(
        "mode = \"light\"\n"
        "accent = \"#3457d5\"\n"
        "background = \"#606060\"\n"
        "foreground = \"#101010\"\n")).toUtf8());

    SystemTheme theme;
    QVERIFY(theme.hasThemeColors());
    QVERIFY(!theme.darkMode());
}

void TestTheme::themeSwitchIsPickedUpLive()
{
    const QString path = writeColors(QStringLiteral(
        "accent = \"#69c3ff\"\n"
        "background = \"#111422\"\n"
        "foreground = \"#bcc1dc\"\n"));
    qputenv("OMANTA_COLORS_FILE", path.toUtf8());

    SystemTheme theme;
    QVERIFY(theme.hasThemeColors());
    QCOMPARE(theme.accentColor(), QColor("#69c3ff"));

    QSignalSpy spy(&theme, &SystemTheme::paletteChanged);
    writeColors(QStringLiteral(
        "accent = \"#a6e3a1\"\n"
        "background = \"#fbf1c7\"\n"
        "foreground = \"#3c3836\"\n"));

    QVERIFY(spy.wait(5000));
    QCOMPARE(theme.accentColor(), QColor("#a6e3a1"));
    QVERIFY(!theme.darkMode()); // the rewrite flipped it light
}

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
