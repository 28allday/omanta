#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

class QDBusVariant;
class QFileSystemWatcher;

// Follows the desktop's appearance. Two sources, in order of authority:
//
//  1. The active Omarchy theme's colors.toml, materialized by omarchy-theme-set
//     at ~/.local/state/omarchy/current/theme/colors.toml. When it exists, the
//     palette IS the theme — every colour role below comes from it, and a theme
//     switch is picked up live by watching the file.
//  2. org.freedesktop.portal.Settings for dark/light and text scale — the same
//     mechanism omacalc uses. On a non-Omarchy system this is all there is, and
//     the QML side falls back to its built-in palette.
//
// colors.toml comes in two shapes: stock themes carry the full set (mode,
// dark_background, lighter_background, dark_foreground, ...), while themes
// converted from the old format get a minimal one (accent, selection,
// background, foreground, color0-15). Missing roles are derived by blending,
// and dark/light is inferred from background luminance when `mode` is absent.
class SystemTheme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textScaleChanged)

    Q_PROPERTY(bool hasThemeColors READ hasThemeColors NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowColor READ windowColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor chromeColor READ chromeColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor textDimColor READ textDimColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor selectionTextColor READ selectionTextColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor hoverColor READ hoverColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor errorColor READ errorColor NOTIFY paletteChanged)

public:
    explicit SystemTheme(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }

    bool hasThemeColors() const { return m_hasThemeColors; }
    QColor windowColor() const { return m_window; }
    QColor chromeColor() const { return m_chrome; }
    QColor borderColor() const { return m_border; }
    QColor textColor() const { return m_text; }
    QColor textDimColor() const { return m_textDim; }
    QColor accentColor() const { return m_accent; }
    QColor selectionColor() const { return m_selection; }
    QColor selectionTextColor() const { return m_selectionText; }
    QColor hoverColor() const { return m_hover; }
    QColor errorColor() const { return m_error; }

    // The flat `key = "value"` subset of TOML that colors.toml uses. Exposed
    // for the unit test; deliberately not a general TOML parser.
    static QHash<QString, QString> parseColorsToml(const QString &text);

Q_SIGNALS:
    void darkModeChanged();
    void textScaleChanged();
    void paletteChanged();

private Q_SLOTS:
    void handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                    const QDBusVariant &value);

private:
    void requestPortalDarkMode();
    void requestPortalTextScale();
    void applyColorScheme(uint scheme);
    void setDarkMode(bool dark);
    void setTextScale(qreal scale);

    QString themeColorsPath() const;
    void watchThemeColors();
    void loadThemeColors();

    bool m_darkMode = true;
    qreal m_textScale = 1.0;

    bool m_hasThemeColors = false;
    QColor m_window;
    QColor m_chrome;
    QColor m_border;
    QColor m_text;
    QColor m_textDim;
    QColor m_accent;
    QColor m_selection;
    QColor m_selectionText;
    QColor m_hover;
    QColor m_error;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_reloadSettle;
};
