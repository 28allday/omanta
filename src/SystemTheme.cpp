#include "SystemTheme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>

namespace {

constexpr const char *kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kPortalSettings = "org.freedesktop.portal.Settings";
constexpr const char *kAppearance = "org.freedesktop.appearance";

QDBusMessage readOne(const QString &key)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(kPortalService), QLatin1String(kPortalPath),
        QLatin1String(kPortalSettings), QStringLiteral("ReadOne"));
    message << QLatin1String(kAppearance) << key;
    return message;
}

// A weighted blend in sRGB. Enough for deriving hover/border/dim-text shades
// from the two anchors every theme provides.
QColor mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

} // namespace

SystemTheme::SystemTheme(QObject *parent)
    : QObject(parent)
{
    // Qt already tracks the colour scheme on most setups; the portal is the
    // authority when it answers, and this is a sane value until it does.
    if (auto *hints = QGuiApplication::styleHints())
        m_darkMode = hints->colorScheme() != Qt::ColorScheme::Light;

    QDBusConnection::sessionBus().connect(
        QLatin1String(kPortalService), QLatin1String(kPortalPath),
        QLatin1String(kPortalSettings), QStringLiteral("SettingChanged"), this,
        SLOT(handlePortalSettingChanged(QString, QString, QDBusVariant)));

    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
            if (!m_hasThemeColors)
                setDarkMode(scheme != Qt::ColorScheme::Light);
        });
    }

    requestPortalDarkMode();
    requestPortalTextScale();

    // omarchy-theme-set rewrites the whole current/theme directory, which
    // arrives as a burst of watcher events; settle before re-reading.
    m_reloadSettle.setSingleShot(true);
    m_reloadSettle.setInterval(200);
    connect(&m_reloadSettle, &QTimer::timeout, this, [this] {
        loadThemeColors();
        watchThemeColors();
    });

    loadThemeColors();
    watchThemeColors();
}

void SystemTheme::setDarkMode(bool dark)
{
    if (m_darkMode == dark)
        return;
    m_darkMode = dark;
    Q_EMIT darkModeChanged();
}

void SystemTheme::setTextScale(qreal scale)
{
    if (qFuzzyCompare(m_textScale, scale) || scale <= 0)
        return;
    m_textScale = scale;
    Q_EMIT textScaleChanged();
}

void SystemTheme::applyColorScheme(uint scheme)
{
    // 0 = no preference, 1 = prefer dark, 2 = prefer light. The Omarchy theme
    // outranks the portal: when colors.toml is loaded, the palette decides.
    if (m_hasThemeColors)
        return;
    if (scheme == 1)
        setDarkMode(true);
    else if (scheme == 2)
        setDarkMode(false);
}

void SystemTheme::requestPortalDarkMode()
{
    auto pending = QDBusConnection::sessionBus().asyncCall(readOne(QStringLiteral("color-scheme")));
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QDBusVariant> reply = *call;
        if (!reply.isError())
            applyColorScheme(reply.value().variant().toUInt());
        call->deleteLater();
    });
}

void SystemTheme::requestPortalTextScale()
{
    auto pending = QDBusConnection::sessionBus().asyncCall(readOne(QStringLiteral("text-scaling-factor")));
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QDBusVariant> reply = *call;
        if (!reply.isError()) {
            const double scale = reply.value().variant().toDouble();
            if (scale > 0)
                setTextScale(scale);
        }
        call->deleteLater();
    });
}

void SystemTheme::handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                             const QDBusVariant &value)
{
    if (nameSpace != QLatin1String(kAppearance))
        return;

    if (key == QLatin1String("color-scheme"))
        applyColorScheme(value.variant().toUInt());
    else if (key == QLatin1String("text-scaling-factor"))
        setTextScale(value.variant().toDouble());
}

// ---- Omarchy theme colours -------------------------------------------------

QHash<QString, QString> SystemTheme::parseColorsToml(const QString &text)
{
    // colors.toml is flat `key = "value"` lines. Parsed by hand: QSettings
    // mangles values (it treats `;` and `#` specially), and pulling in a TOML
    // library for twenty flat keys is not worth a dependency.
    QHash<QString, QString> values;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        // Strip a trailing comment only outside quotes, then the quotes.
        if (value.startsWith(QLatin1Char('"'))) {
            const qsizetype closing = value.indexOf(QLatin1Char('"'), 1);
            if (closing < 0)
                continue;
            value = value.mid(1, closing - 1);
        } else {
            const qsizetype hash = value.indexOf(QLatin1Char('#'));
            if (hash >= 0)
                value = value.left(hash).trimmed();
        }
        if (!key.isEmpty() && !value.isEmpty())
            values.insert(key, value);
    }
    return values;
}

QString SystemTheme::themeColorsPath() const
{
    // Test override first; otherwise where omarchy-theme-set materializes the
    // active theme. GenericStateLocation honours XDG_STATE_HOME.
    const QString override = qEnvironmentVariable("OMANTA_COLORS_FILE");
    if (!override.isEmpty())
        return override;

    const QString state = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    return state + QStringLiteral("/omarchy/current/theme/colors.toml");
}

void SystemTheme::watchThemeColors()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                [this] { m_reloadSettle.start(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                [this] { m_reloadSettle.start(); });
    }

    const QStringList watched = m_watcher->files() + m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);

    // Watch the file for edits and its directory for the replace-on-theme-set
    // case, where the file's inode goes away and a plain file watch dies with
    // it. If neither exists yet, watch the nearest existing ancestor so a
    // first `omarchy theme set` is still noticed.
    const QString path = themeColorsPath();
    if (QFile::exists(path))
        m_watcher->addPath(path);
    QDir dir = QFileInfo(path).dir();
    while (!dir.exists() && !dir.isRoot())
        dir.cdUp();
    if (dir.exists())
        m_watcher->addPath(dir.absolutePath());
}

void SystemTheme::loadThemeColors()
{
    QFile file(themeColorsPath());
    QHash<QString, QString> values;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        values = parseColorsToml(QString::fromUtf8(file.readAll()));

    const QColor background(values.value(QStringLiteral("background")));
    const QColor foreground(values.value(QStringLiteral("foreground")));
    const QColor accent(values.value(QStringLiteral("accent")));

    const bool usable = background.isValid() && foreground.isValid() && accent.isValid();
    if (!usable) {
        if (m_hasThemeColors) {
            m_hasThemeColors = false;
            Q_EMIT paletteChanged();
        }
        return;
    }

    // Stock themes say so; converted ones are judged by their background.
    const QString mode = values.value(QStringLiteral("mode"));
    const bool dark = mode.isEmpty() ? background.lightnessF() < 0.5
                                     : mode == QLatin1String("dark");

    const auto role = [&values](const char *key) { return QColor(values.value(QLatin1String(key))); };

    m_window = background;
    m_text = foreground;
    m_accent = accent;

    const QColor selection = role("selection");
    m_selection = selection.isValid() ? selection : mix(background, accent, 0.3);

    // Converted themes sometimes reuse the foreground as the selection colour
    // (Arc Blueberry does), which would render selected labels invisible.
    // Whichever of text and background sits further from the selection in
    // lightness is the one a selected label is readable in.
    const qreal fromText = qAbs(foreground.lightnessF() - m_selection.lightnessF());
    const qreal fromBackground = qAbs(background.lightnessF() - m_selection.lightnessF());
    m_selectionText = fromText >= fromBackground ? foreground : background;

    // Roles the minimal format lacks are blends between the two anchors —
    // subtle steps from the background toward the text, same as the built-in
    // palette's spacing.
    const QColor chrome = role("dark_background");
    m_chrome = chrome.isValid() ? chrome : mix(background, foreground, 0.05);

    const QColor hover = role("lighter_background");
    m_hover = hover.isValid() ? hover : mix(background, foreground, 0.09);

    const QColor dim = role("dark_foreground");
    m_textDim = dim.isValid() ? dim : mix(foreground, background, 0.4);

    m_border = mix(background, foreground, 0.16);

    const QColor red = role("red").isValid() ? role("red") : role("color1");
    m_error = red.isValid() ? red : QColor(QStringLiteral("#f7768e"));

    m_hasThemeColors = true;
    setDarkMode(dark);
    Q_EMIT paletteChanged();
}
