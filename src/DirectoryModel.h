#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QtQmlIntegration>

#include <gio/gio.h>

#include "FileEntry.h"

// A live listing of one directory.
//
// Two things Phase 0 taught this class:
//   - Never reload wholesale. A single `touch` produces CREATED + CHANGED +
//     CHANGES_DONE, and a directory being written to produces a continuous
//     stream. Changes are coalesced and applied in place.
//   - Never touch the monitor from inside its own callback.
//
// The model is deliberately unsorted — rows arrive in enumeration order and
// stay put. Ordering and filtering are FileSortFilterModel's job, which keeps
// insertion O(1) and means a sort change never re-reads the filesystem.
class DirectoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    // Directory item counts ("12 items" for folders). Off by default; the tab
    // turns it on per the show-directory-item-counts preference and locality,
    // the same shape as the thumbnail policy. Counts arrive asynchronously,
    // one folder at a time, and land as dataChanged on the folder's row.
    Q_PROPERTY(bool countItems READ countItems WRITE setCountItems NOTIFY countItemsChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DisplayNameRole,
        FilePathRole,
        IsDirRole,
        IsHiddenRole,
        IsBackupRole,
        IsSymlinkRole,
        SizeRole,
        ModifiedRole,
        CreatedRole,
        AccessedRole,
        OwnerRole,
        GroupRole,
        PermissionsRole, // "drwxr-xr-x", empty where the mode is unknown
        ContentTypeRole,
        TypeDescriptionRole,
        IconSourceRole,
        OrigPathRole, // trash::orig-path; empty outside trash:///
        TargetPathRole, // standard::target-uri; set in recent:/// and friends
        ItemCountRole,    // folders: visible children, -1 unknown
        ItemCountAllRole, // folders: every child, -1 unknown
        DepthRole,        // tree indentation level; flat models answer 0
        ExpandedRole,     // tree expansion state; flat models answer false
    };
    Q_ENUM(Roles)

    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool countItems() const { return m_countItems; }
    void setCountItems(bool enabled);
    bool loading() const { return m_loading; }
    int count() const { return int(m_entries.size()); }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString filePathAt(int row) const;
    Q_INVOKABLE int indexOfName(const QString &name) const;

    // Every name in the folder, hidden files included — batch rename checks
    // its conflicts against this, and a hidden file can clash too.
    Q_INVOKABLE QStringList allNames() const;

Q_SIGNALS:
    void pathChanged();
    // The location exists but its filesystem is not mounted yet — the cue to
    // run a mount (with credentials if asked) and reload.
    void needsMount(const QString &location);
    void loadingChanged();
    void countChanged();
    void countItemsChanged();
    void errorMessageChanged();

private:
    // Async GIO callbacks can outlive the model or, more often, outlive the
    // load that issued them. Every callback carries a weak pointer plus the
    // generation it belongs to, and results from a superseded load are dropped.
    struct CallbackCtx {
        QPointer<DirectoryModel> model;
        quint64 generation = 0;
        QString name; // set for single-file refreshes
    };

    // One settle window's worth of changes, applied to the model together.
    //
    // Each changed name is queried independently and the answers arrive in any
    // order, so they are collected here and applied once the last one lands.
    // Copying a thousand files into a folder then costs one insert signal and
    // one index rebuild, not a thousand of each.
    struct RefreshBatch {
        QPointer<DirectoryModel> model;
        quint64 generation = 0;
        int outstanding = 0;
        QList<FileEntry> arrived; // files that still exist
        QStringList gone;         // files that no longer do
    };

    struct RefreshCtx {
        RefreshBatch *batch = nullptr;
        QString name;
    };

    // One folder's count in progress. The tallies live here rather than in the
    // entry so a superseded load can drop them without ever touching the model.
    struct CountCtx {
        QPointer<DirectoryModel> model;
        quint64 generation = 0;
        QString name;
        int visible = 0;
        int total = 0;
    };

    static void onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onNextFilesReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onRefreshInfoReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onCountEnumerateReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onCountNextReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onMonitorChanged(GFileMonitor *monitor, GFile *file, GFile *other,
                                 GFileMonitorEvent event, gpointer data);

    void beginLoad();
    void appendBatch(GList *infos);
    void applyBatch(RefreshBatch *batch);
    void applyEntries(const QList<FileEntry> &entries);
    void removeNames(const QStringList &names);
    void rebuildIndex();
    void queueRefresh(const QString &name);
    void flushPending();
    void enqueueCount(const QString &name);
    void enqueueAllCounts();
    void pumpCounts();
    void finishCount(const QString &name, int visible, int total);
    void armMonitor(GFile *dir);
    void teardownMonitor();
    void cancelInFlight();
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);

    QString m_path;
    QList<FileEntry> m_entries;
    QHash<QString, int> m_rowByName;
    QString m_errorMessage;
    quint64 m_generation = 0;
    bool m_loading = false;

    GFile *m_dir = nullptr;
    GCancellable *m_cancellable = nullptr;
    GFileMonitor *m_monitor = nullptr;
    gulong m_monitorHandler = 0;

    // Monitor events are noisy; collect names and settle before hitting disk.
    QSet<QString> m_pendingNames;
    QTimer m_settleTimer;

    // Folder counts are read one directory at a time — a folder of a thousand
    // subfolders must not open a thousand enumerators at once. The queue holds
    // names, not rows: rows move under a live sort.
    bool m_countItems = false;
    QStringList m_countQueue;
    QSet<QString> m_countQueued; // membership mirror of the queue
    bool m_countRunning = false;
};
