#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QtQmlIntegration>

class QFileSystemWatcher;

// User preferences — Nautilus's Preferences dialog, persisted the house way:
// a plain watched file (~/.config/omanta/settings, key=value per line,
// OMANTA_SETTINGS_FILE overrides for tests), not gsettings. Defaults match
// Omarchy's out-of-the-box Nautilus, which is GNOME schema defaults — that is
// the parity contract, not this machine's dconf.
//
// Values are validated on read: an unknown value in the file falls back to
// the default rather than leaking a bad state into every binding.
class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // General
    Q_PROPERTY(bool sortFoldersFirst READ sortFoldersFirst WRITE setSortFoldersFirst NOTIFY changed)
    Q_PROPERTY(QString clickPolicy READ clickPolicy WRITE setClickPolicy NOTIFY changed)
    // Nautilus's list-view use-tree-view: expandable folders, false by default.
    Q_PROPERTY(bool useTreeView READ useTreeView WRITE setUseTreeView NOTIFY changed)

    // Optional context menu actions (the shortcuts work regardless)
    Q_PROPERTY(bool showCreateLink READ showCreateLink WRITE setShowCreateLink NOTIFY changed)
    Q_PROPERTY(bool showDeletePermanently READ showDeletePermanently WRITE setShowDeletePermanently NOTIFY changed)

    // Performance ("never" | "local-only" | "always")
    Q_PROPERTY(QString searchInSubfolders READ searchInSubfolders WRITE setSearchInSubfolders NOTIFY changed)
    Q_PROPERTY(QString showThumbnails READ showThumbnails WRITE setShowThumbnails NOTIFY changed)
    Q_PROPERTY(QString showDirectoryItemCounts READ showDirectoryItemCounts WRITE setShowDirectoryItemCounts NOTIFY changed)

    // Date and time format ("simple" | "detailed")
    Q_PROPERTY(QString dateTimeFormat READ dateTimeFormat WRITE setDateTimeFormat NOTIFY changed)

    // Not in the dialog: the view mode new tabs start in. Nautilus persists
    // the last-chosen view as default-folder-viewer; switching views here
    // writes this the same way.
    Q_PROPERTY(QString defaultViewMode READ defaultViewMode WRITE setDefaultViewMode NOTIFY changed)
    // Nautilus's show-hidden-files: Ctrl+H flips it and it sticks across
    // restarts (key "showHiddenFiles" in ~/.config/omanta/settings).
    Q_PROPERTY(bool showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles NOTIFY changed)
    // The sidebar in a wide window: F9 flips it and it sticks (Nautilus's
    // start-with-sidebar). A narrow window hides it regardless.
    Q_PROPERTY(bool showSidebar READ showSidebar WRITE setShowSidebar NOTIFY changed)

    // List-view columns, Nautilus's two-key shape: the full order (every
    // column id, reorderable) and the visible subset. Name is always visible
    // and always first; visible defaults are the GNOME schema defaults.
    Q_PROPERTY(QStringList listColumnOrder READ listColumnOrder WRITE setListColumnOrder NOTIFY changed)
    Q_PROPERTY(QStringList listVisibleColumns READ listVisibleColumns WRITE setListVisibleColumns NOTIFY changed)

    // Icon-view captions: exactly three slots, "none" filling the gaps —
    // Nautilus's captions key, whose schema default is no captions at all.
    Q_PROPERTY(QStringList iconCaptions READ iconCaptions WRITE setIconCaptions NOTIFY changed)

    // Not a Nautilus row: the alpha the window's surfaces are painted at,
    // the way a terminal's background_opacity works — backdrop translucent,
    // text and controls opaque. 1 = solid, floor 0.5 keeps content readable.
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY changed)
    // Icon sizes, one per view, shared by every window and remembered:
    // Nautilus keeps its zoom levels as global settings too.
    Q_PROPERTY(int iconZoom READ iconZoom WRITE setIconZoom NOTIFY changed)
    Q_PROPERTY(int listZoom READ listZoom WRITE setListZoom NOTIFY changed)
    // Context-menu actions switched off in Preferences, by action id (the
    // TOML file's basename). Empty by default: every installed action shows.
    Q_PROPERTY(QStringList hiddenActions READ hiddenActions WRITE setHiddenActions NOTIFY changed)
    // Bookkeeping, not a preference: the Toggle-menu row is added on first
    // launch only, so removing it sticks.
    Q_PROPERTY(bool toggleMenuOffered READ toggleMenuOffered WRITE setToggleMenuOffered NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    bool sortFoldersFirst() const { return boolFor("sortFoldersFirst", true); }
    QString clickPolicy() const { return choiceFor("clickPolicy", {"double", "single"}); }
    bool useTreeView() const { return boolFor("useTreeView", false); }
    bool showCreateLink() const { return boolFor("showCreateLink", false); }
    bool showDeletePermanently() const { return boolFor("showDeletePermanently", false); }
    QString searchInSubfolders() const { return choiceFor("searchInSubfolders", {"local-only", "never", "always"}); }
    QString showThumbnails() const { return choiceFor("showThumbnails", {"local-only", "never", "always"}); }
    QString showDirectoryItemCounts() const { return choiceFor("showDirectoryItemCounts", {"local-only", "never", "always"}); }
    QString dateTimeFormat() const { return choiceFor("dateTimeFormat", {"simple", "detailed"}); }
    QString defaultViewMode() const { return choiceFor("defaultViewMode", {"icon", "list"}); }
    bool showHiddenFiles() const { return boolFor("showHiddenFiles", false); }
    bool showSidebar() const { return boolFor("showSidebar", true); }
    QStringList listColumnOrder() const;
    QStringList listVisibleColumns() const;
    QStringList iconCaptions() const;
    qreal backgroundOpacity() const { return realFor("backgroundOpacity", 1.0, 0.5, 1.0); }
    bool toggleMenuOffered() const { return boolFor("toggleMenuOffered", false); }
    int iconZoom() const { return qRound(realFor("iconZoom", 64, 32, 128)); }
    int listZoom() const { return qRound(realFor("listZoom", 18, 16, 64)); }
    QStringList hiddenActions() const { return m_values.value(QStringLiteral("hiddenActions")).split(QLatin1Char(','), Qt::SkipEmptyParts); }

    // Every column id, canonical order. The QML layer owns labels and widths.
    Q_INVOKABLE static QStringList allListColumns();
    // Valid caption fields — the columns minus name (the name is the label).
    Q_INVOKABLE static QStringList allCaptionFields();

    // The value chosen for a context-menu action's `option`, keyed by action
    // id ("actionOption.<id>" in the file). Unvalidated here: only the action
    // knows its choices, and UserActions falls back to the first.
    Q_INVOKABLE QString actionOption(const QString &id) const { return m_values.value(QStringLiteral("actionOption.") + id); }
    Q_INVOKABLE void setActionOption(const QString &id, const QString &value) { set(QStringLiteral("actionOption.") + id, value); }

    void setSortFoldersFirst(bool value) { set("sortFoldersFirst", value ? "true" : "false"); }
    void setClickPolicy(const QString &value) { set("clickPolicy", value); }
    void setUseTreeView(bool value) { set("useTreeView", value ? "true" : "false"); }
    void setShowCreateLink(bool value) { set("showCreateLink", value ? "true" : "false"); }
    void setShowDeletePermanently(bool value) { set("showDeletePermanently", value ? "true" : "false"); }
    void setSearchInSubfolders(const QString &value) { set("searchInSubfolders", value); }
    void setShowThumbnails(const QString &value) { set("showThumbnails", value); }
    void setShowDirectoryItemCounts(const QString &value) { set("showDirectoryItemCounts", value); }
    void setDateTimeFormat(const QString &value) { set("dateTimeFormat", value); }
    void setDefaultViewMode(const QString &value) { set("defaultViewMode", value); }
    void setShowHiddenFiles(bool value) { set("showHiddenFiles", value ? "true" : "false"); }
    void setShowSidebar(bool value) { set("showSidebar", value ? "true" : "false"); }
    void setListColumnOrder(const QStringList &value) { set("listColumnOrder", value.join(QLatin1Char(','))); }
    void setListVisibleColumns(const QStringList &value) { set("listVisibleColumns", value.join(QLatin1Char(','))); }
    void setIconCaptions(const QStringList &value) { set("iconCaptions", value.join(QLatin1Char(','))); }
    void setBackgroundOpacity(qreal value) { set("backgroundOpacity", QString::number(value, 'f', 2)); }
    void setToggleMenuOffered(bool value) { set("toggleMenuOffered", value ? "true" : "false"); }
    void setIconZoom(int value) { set("iconZoom", QString::number(value)); }
    void setListZoom(int value) { set("listZoom", QString::number(value)); }
    void setHiddenActions(const QStringList &value) { set("hiddenActions", value.join(QLatin1Char(','))); }

Q_SIGNALS:
    // One signal for the lot: preference flips are rare and every consumer
    // re-reading one cheap hash lookup is simpler than eight notify chains.
    void changed();

private:
    QString filePath() const;
    void load();
    void save();
    void set(const QString &key, const QString &value);
    bool boolFor(const char *key, bool fallback) const;
    // The first entry of `allowed` is the default.
    QString choiceFor(const char *key, const QStringList &allowed) const;
    // Comma list from the file, unknown ids and duplicates dropped.
    QStringList columnListFor(const char *key, const QStringList &fallback) const;
    // A number inside [min, max]; anything else falls back to the default.
    qreal realFor(const char *key, qreal fallback, qreal min, qreal max) const;

    QHash<QString, QString> m_values;
    QFileSystemWatcher *m_watcher = nullptr;
    bool m_saving = false;
};
