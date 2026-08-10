#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

#include <gio/gio.h>

// Everything the properties dialog shows about one selection.
//
// Instantiated per dialog rather than being a singleton: two windows can have
// properties open on different files at once, and each needs its own in-flight
// measurement to cancel when its dialog closes.
//
// Every read is async. A folder total means walking the tree, and a tree can be
// a mounted share that has stopped answering — doing that on the GUI thread
// would freeze the window, which is the one thing the threading rule forbids.
class FileProperties : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList paths READ paths WRITE setPaths NOTIFY pathsChanged)

    // Descriptive facts, all valid once `loaded` turns true.
    Q_PROPERTY(bool loaded READ loaded NOTIFY infoChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY infoChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY pathsChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY infoChanged)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY infoChanged)
    Q_PROPERTY(QString contentType READ contentType NOTIFY infoChanged)
    Q_PROPERTY(QString typeDescription READ typeDescription NOTIFY infoChanged)
    Q_PROPERTY(QString location READ location NOTIFY infoChanged)
    Q_PROPERTY(bool isDir READ isDir NOTIFY infoChanged)
    Q_PROPERTY(bool isSymlink READ isSymlink NOTIFY infoChanged)
    Q_PROPERTY(QString symlinkTarget READ symlinkTarget NOTIFY infoChanged)
    Q_PROPERTY(QDateTime modified READ modified NOTIFY infoChanged)
    Q_PROPERTY(QDateTime accessed READ accessed NOTIFY infoChanged)
    Q_PROPERTY(QDateTime created READ created NOTIFY infoChanged)
    Q_PROPERTY(QString owner READ owner NOTIFY infoChanged)
    Q_PROPERTY(QString group READ group NOTIFY infoChanged)

    // Size arrives progressively: a folder's total climbs while the walk runs,
    // so the dialog shows a number moving rather than an empty field.
    Q_PROPERTY(qint64 size READ size NOTIFY measureChanged)
    Q_PROPERTY(qint64 sizeOnDisk READ sizeOnDisk NOTIFY measureChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY measureChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY measureChanged)
    Q_PROPERTY(bool measuring READ measuring NOTIFY measureChanged)

    // Permissions. `mode` is -1 when unknown or when a multi-selection
    // disagrees, which is why the dialog only offers editing for one item.
    Q_PROPERTY(int mode READ mode NOTIFY infoChanged)
    Q_PROPERTY(QString modeText READ modeText NOTIFY infoChanged)
    Q_PROPERTY(QString modeOctal READ modeOctal NOTIFY infoChanged)
    Q_PROPERTY(bool canChangeMode READ canChangeMode NOTIFY infoChanged)

    Q_PROPERTY(QString filesystemType READ filesystemType NOTIFY filesystemChanged)
    Q_PROPERTY(qint64 filesystemFree READ filesystemFree NOTIFY filesystemChanged)
    Q_PROPERTY(qint64 filesystemSize READ filesystemSize NOTIFY filesystemChanged)

public:
    explicit FileProperties(QObject *parent = nullptr);
    ~FileProperties() override;

    QStringList paths() const { return m_paths; }
    void setPaths(const QStringList &paths);

    bool loaded() const { return m_loaded; }
    QString errorMessage() const { return m_errorMessage; }
    int itemCount() const { return int(m_paths.size()); }
    QString displayName() const { return m_displayName; }
    QString iconSource() const;
    QString contentType() const { return m_contentType; }
    QString typeDescription() const { return m_typeDescription; }
    QString location() const { return m_location; }
    bool isDir() const { return m_isDir; }
    bool isSymlink() const { return m_isSymlink; }
    QString symlinkTarget() const { return m_symlinkTarget; }
    QDateTime modified() const { return m_modified; }
    QDateTime accessed() const { return m_accessed; }
    QDateTime created() const { return m_created; }
    QString owner() const { return m_owner; }
    QString group() const { return m_group; }

    qint64 size() const { return m_size + m_liveSize; }
    qint64 sizeOnDisk() const { return m_sizeOnDisk; }
    int fileCount() const { return m_fileCount + m_liveFiles; }
    int folderCount() const { return m_folderCount + m_liveFolders; }
    bool measuring() const { return m_measuring; }

    int mode() const { return m_mode; }
    QString modeText() const { return modeToText(m_mode); }
    QString modeOctal() const;
    bool canChangeMode() const { return m_canChangeMode; }

    QString filesystemType() const { return m_filesystemType; }
    qint64 filesystemFree() const { return m_filesystemFree; }
    qint64 filesystemSize() const { return m_filesystemSize; }

    // "rwxr-xr-x", with the setuid/setgid/sticky bits rendered the way ls
    // renders them. -1 (unknown or mixed) gives an empty string.
    static QString modeToText(int mode);

    Q_INVOKABLE void reload();

    // Applications registered for this file's type, best first, each a map of
    // id/name/iconSource/isDefault. Empty for anything but a single file.
    Q_INVOKABLE QVariantList applications() const;
    Q_INVOKABLE bool launchWith(const QString &applicationId) const;
    Q_INVOKABLE bool setDefaultApplication(const QString &applicationId);

    // Writes the permission bits. The dialog applies on request rather than on
    // every checkbox: a half-typed permission set is not one anybody meant.
    Q_INVOKABLE void applyMode(int mode);

Q_SIGNALS:
    void pathsChanged();
    void infoChanged();
    void measureChanged();
    void filesystemChanged();

private:
    // GIO callbacks outlive whatever asked for the data — the dialog can be
    // closed mid-walk. Each carries a weak pointer and the generation it was
    // issued for, and answers from a superseded load are dropped. Same rule as
    // DirectoryModel, for the same reason.
    struct CallbackCtx {
        QPointer<FileProperties> self;
        quint64 generation = 0;
        int index = 0;
    };

    static void onInfoReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onFilesystemReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onMeasureReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onMeasureProgress(gboolean reporting, guint64 currentSize, guint64 numDirs,
                                  guint64 numFiles, gpointer data);
    static void onModeApplied(GObject *source, GAsyncResult *res, gpointer data);

    void load();
    void reset();
    void applyPrimaryInfo(GFileInfo *info);
    void accountFor(GFileInfo *info, int index);
    void startNextMeasure();
    void cancelInFlight();
    void setError(const QString &message);

    QStringList m_paths;
    quint64 m_generation = 0;
    bool m_loaded = false;
    QString m_errorMessage;

    QString m_displayName;
    QStringList m_iconNames;
    QString m_contentType;
    QString m_typeDescription;
    QString m_location;
    bool m_isDir = false;
    bool m_isSymlink = false;
    QString m_symlinkTarget;
    QDateTime m_modified;
    QDateTime m_accessed;
    QDateTime m_created;
    QString m_owner;
    QString m_group;

    // Settled totals, plus what the walk in progress has found so far. Keeping
    // them apart is what lets a running measurement be reported without
    // double-counting it when it finishes.
    qint64 m_size = 0;
    qint64 m_sizeOnDisk = 0;
    int m_fileCount = 0;
    int m_folderCount = 0;
    qint64 m_liveSize = 0;
    int m_liveFiles = 0;
    int m_liveFolders = 0;
    bool m_measuring = false;

    int m_mode = -1;
    bool m_canChangeMode = false;

    QString m_filesystemType;
    qint64 m_filesystemFree = 0;
    qint64 m_filesystemSize = 0;

    // Directories still to walk. They are measured one at a time: several
    // concurrent walks would race on the live counters and thrash the disk for
    // no gain.
    QStringList m_toMeasure;
    int m_outstandingInfo = 0;
    GCancellable *m_cancellable = nullptr;
};
