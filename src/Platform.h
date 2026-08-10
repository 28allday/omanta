#pragma once

#include <QObject>
#include <QString>

// Small bridge for the things QML cannot do itself: launching files with their
// default application, opening a terminal, and formatting sizes the way the
// rest of the desktop does.
class Platform : public QObject
{
    Q_OBJECT

public:
    explicit Platform(QObject *parent = nullptr);

    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE QString parentPath(const QString &path) const;
    Q_INVOKABLE QString baseName(const QString &path) const;
    Q_INVOKABLE bool isDir(const QString &path) const;
    Q_INVOKABLE bool exists(const QString &path) const;

    // Locations: a path or a URI (trash:///, smb://server/share, ...).
    // isLocal gates the things that genuinely need a filesystem path —
    // terminals, sync stat checks. isNavigable is the navigation test:
    // a local path must be a directory, while a URI is taken on trust and
    // the model's error reporting deals with the lie — a synchronous stat
    // of smb:// would block the UI for as long as the network feels like.
    Q_INVOKABLE bool isLocal(const QString &location) const;
    Q_INVOKABLE bool isNavigable(const QString &location) const;

    // URI schemes gvfs can actually mount here ("smb", "sftp", …) — what the
    // Network view's protocol help may honestly advertise.
    Q_INVOKABLE QStringList supportedSchemes() const;

    // The user's document templates (XDG Templates dir), each a {name, path}
    // map, name-sorted; empty when the dir is missing, empty, or disabled
    // (XDG defines "equals $HOME" as disabled). Feeds the context menu's
    // New Document submenu. OMANTA_TEMPLATES_DIR overrides (tests).
    Q_INVOKABLE QVariantList templates() const;

    // Whether "Extract Here" applies — the content types the archive engine
    // can actually open.
    Q_INVOKABLE bool isArchiveType(const QString &contentType) const;

    // Nautilus 50's archive-activation rule: double-clicking an archive
    // extracts it if and only if the system default handler for that type is
    // the file manager itself; otherwise it opens with the default app.
    Q_INVOKABLE bool activationExtracts(const QString &contentType) const;

    // "photos.tar.gz" → "photos": the suggested name for the Compress dialog
    // and the landing folder rule, compound-extension aware.
    Q_INVOKABLE QString archiveStem(const QString &name) const;

    // Drag and drop. uriList renders locations as the text/uri-list payload a
    // drag carries; locationsFromUrls is its inverse for a drop's URLs.
    Q_INVOKABLE QString uriList(const QStringList &locations) const;
    Q_INVOKABLE QStringList locationsFromUrls(const QVariantList &urls) const;

    // Whether two locations live on the same filesystem — what decides if an
    // unmodified drop means move (same) or copy (different), Nautilus's rule.
    // Synchronous; on a remote location this can touch the network, which is
    // accepted for the same reason as collisions(): it runs once, on a drop.
    Q_INVOKABLE bool sameFilesystem(const QString &a, const QString &b) const;

    // The modifiers held right now. Drop events in QML don't carry them, and
    // Ctrl-means-copy has to be read at the moment of the drop.
    Q_INVOKABLE int keyboardModifiers() const;

    // Human-readable size using GLib's formatter, so omanta and every GTK
    // app on the system agree on what "1.2 MB" means.
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    // "1 item" / "12 items", "—" for a count of -1 (unknown).
    Q_INVOKABLE QString formatItemCount(int count) const;
    // format: "simple" (Nautilus's relative style — "Today, 12:33", "3 days
    // ago") or "detailed" (full numeric date and time). Anything else is
    // treated as simple.
    Q_INVOKABLE QString formatModified(const QDateTime &when,
                                       const QString &format = QStringLiteral("simple")) const;

    // The unabbreviated form, for the properties dialog: a listing wants "14:22",
    // but "when exactly was this touched?" wants the date and the seconds.
    Q_INVOKABLE QString formatTimestamp(const QDateTime &when) const;

    // Hands the file to whatever the desktop considers its default handler.
    // Returns false if nothing is registered for the type.
    Q_INVOKABLE bool openPath(const QString &path) const;

    // Opens the user's terminal in `directory`. Resolves through $TERMINAL and
    // the xdg-terminal-exec convention rather than naming a terminal, so this
    // keeps working whatever the user has installed.
    Q_INVOKABLE bool openTerminal(const QString &directory) const;

    // Turns whatever the user typed into an absolute path: expands a leading
    // ~, and resolves a relative path against the folder being viewed.
    Q_INVOKABLE QString resolvePath(const QString &input, const QString &base) const;

    // Names that already exist in `destinationDir`. The UI asks about
    // conflicts before starting an operation rather than mid-flight, because a
    // dialog that interrupts a running copy is far harder to get right — and
    // far easier to dismiss by accident — than one that asks up front.
    Q_INVOKABLE QStringList collisions(const QStringList &paths,
                                       const QString &destinationDir) const;

    // Splits a path into its components for the breadcrumb bar.
    Q_INVOKABLE QVariantList pathCrumbs(const QString &path) const;
};
