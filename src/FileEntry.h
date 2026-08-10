#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <gio/gio.h>

// One row in a directory listing. Deliberately a plain value type: the model
// holds a QList of these and hands fields out by role, so nothing in the view
// layer ever touches a GObject.
struct FileEntry
{
    QString name;         // on-disk name, the stable identity within a directory
    QString displayName;  // what the user should read (may differ on odd encodings)
    bool isDir = false;
    bool isHidden = false;
    bool isBackup = false;
    bool isSymlink = false;
    qint64 size = 0;
    QDateTime modified;
    QDateTime created;    // invalid where the filesystem has no birth time
    QDateTime accessed;
    QString owner;        // owner::user — empty where the backend doesn't say
    QString group;        // owner::group
    quint32 mode = 0;     // unix::mode; 0 means unknown (remote mounts)
    QString contentType;  // MIME type, e.g. "text/plain"
    QString typeDescription;
    QStringList iconNames; // themed icon candidates, best first
    QString origPath;     // trash::orig-path — set only for rows in trash:///
    QString targetPath;   // standard::target-uri as a local path (or URI when
                          // not local) — set for rows in recent:/// and other
                          // backends whose entries point at a file elsewhere

    // Directory item counts, filled in by DirectoryModel's counting pass —
    // never by fromInfo(), because GIO has no child-count attribute. -1 means
    // unknown (not yet counted, counting disabled, or the folder unreadable).
    // Two tallies from one enumeration: Nautilus counts what you would see
    // (hidden and backup files only when shown), so Ctrl+H switches which
    // number is displayed without touching the disk again.
    int itemCount = -1;    // children that are not hidden/backup
    int itemCountAll = -1; // every child

    // "drwxr-xr-x" from mode, or "" while mode is unknown.
    QString permissionString() const;

    // Everything the views need, pulled in one enumerate pass. Adding an
    // attribute here costs nothing extra per file; querying it later costs a
    // syscall per file, so ask for it up front.
    static const char *queryAttributes();

    static FileEntry fromInfo(GFileInfo *info);
};
