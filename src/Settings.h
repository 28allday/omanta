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

    // List-view columns, Nautilus's two-key shape: the full order (every
    // column id, reorderable) and the visible subset. Name is always visible
    // and always first; visible defaults are the GNOME schema defaults.
    Q_PROPERTY(QStringList listColumnOrder READ listColumnOrder WRITE setListColumnOrder NOTIFY changed)
    Q_PROPERTY(QStringList listVisibleColumns READ listVisibleColumns WRITE setListVisibleColumns NOTIFY changed)

    // Icon-view captions: exactly three slots, "none" filling the gaps —
    // Nautilus's captions key, whose schema default is no captions at all.
    Q_PROPERTY(QStringList iconCaptions READ iconCaptions WRITE setIconCaptions NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    bool sortFoldersFirst() const { return boolFor("sortFoldersFirst", false); }
    QString clickPolicy() const { return choiceFor("clickPolicy", {"double", "single"}); }
    bool useTreeView() const { return boolFor("useTreeView", false); }
    bool showCreateLink() const { return boolFor("showCreateLink", false); }
    bool showDeletePermanently() const { return boolFor("showDeletePermanently", false); }
    QString searchInSubfolders() const { return choiceFor("searchInSubfolders", {"local-only", "never", "always"}); }
    QString showThumbnails() const { return choiceFor("showThumbnails", {"local-only", "never", "always"}); }
    QString showDirectoryItemCounts() const { return choiceFor("showDirectoryItemCounts", {"local-only", "never", "always"}); }
    QString dateTimeFormat() const { return choiceFor("dateTimeFormat", {"simple", "detailed"}); }
    QString defaultViewMode() const { return choiceFor("defaultViewMode", {"icon", "list"}); }
    QStringList listColumnOrder() const;
    QStringList listVisibleColumns() const;
    QStringList iconCaptions() const;

    // Every column id, canonical order. The QML layer owns labels and widths.
    Q_INVOKABLE static QStringList allListColumns();
    // Valid caption fields — the columns minus name (the name is the label).
    Q_INVOKABLE static QStringList allCaptionFields();

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
    void setListColumnOrder(const QStringList &value) { set("listColumnOrder", value.join(QLatin1Char(','))); }
    void setListVisibleColumns(const QStringList &value) { set("listVisibleColumns", value.join(QLatin1Char(','))); }
    void setIconCaptions(const QStringList &value) { set("iconCaptions", value.join(QLatin1Char(','))); }

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

    QHash<QString, QString> m_values;
    QFileSystemWatcher *m_watcher = nullptr;
    bool m_saving = false;
};
