#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration>

#include <gio/gio.h>

class Mounter;
class QFileSystemWatcher;

// The sidebar's model: XDG user folders, Trash and Recent, GTK bookmarks
// (shared with Nautilus and the file chooser), and the volumes and mounts
// GVolumeMonitor reports.
//
// Four independent sections, each kept current by its own change source —
// GVolumeMonitor signals for devices, a file watch for bookmarks. A change
// re-derives only its own section's rows, in place; the other sections'
// rows are never touched, so selection and hover state in the view survive
// a USB stick appearing.
class PlacesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Supplies GMountOperations, so a volume that needs credentials asks
    // through the window's one dialog rather than failing.
    Q_PROPERTY(Mounter *mounter READ mounter WRITE setMounter NOTIFY mounterChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        LocationRole,   // navigable location; empty for an unmounted volume
        IconSourceRole, // ready-made image://fileicon/... url
        SectionRole,    // "Places" | "Devices" | "Bookmarks" | "Network"
        MountableRole,  // a volume that needs mounting before it has a location
        EjectableRole,
    };
    Q_ENUM(Roles)

    explicit PlacesModel(QObject *parent = nullptr);
    ~PlacesModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_places.size()); }

    Mounter *mounter() const { return m_mounter; }
    void setMounter(Mounter *mounter);

    // Mount the volume at `row`, then report where it landed. Asynchronous:
    // the result arrives as mounted()/mountFailed(), and the Devices section
    // refreshes itself off the monitor's mount-added signal.
    Q_INVOKABLE void mount(int row);

    // Unmount or eject whatever is at `row`, whichever the device supports.
    Q_INVOKABLE void eject(int row);

    // The row currently showing `location`, or -1 — for highlighting the
    // sidebar entry matching the tab.
    Q_INVOKABLE int rowForLocation(const QString &location) const;

    // Bookmark writes go to the same GTK bookmarks file the section reads, so
    // Nautilus and the file chooser see the change too — and the file watch
    // refreshes this model's own rows.
    Q_INVOKABLE bool isBookmarked(const QString &location) const;
    Q_INVOKABLE void addBookmark(const QString &location);
    Q_INVOKABLE void removeBookmark(const QString &location);

Q_SIGNALS:
    void countChanged();
    void mounterChanged();
    void mounted(const QString &location);
    void mountFailed(const QString &name, const QString &message);

private:
    struct Place {
        QString name;
        QString location;
        QString icon;      // theme icon names, comma-joined
        QString section;
        bool mountable = false;
        bool ejectable = false;
        // Set only for device rows; owned references, released on splice.
        GVolume *volume = nullptr;
        GMount *mount = nullptr;
    };

    static void onVolumeEvent(GVolumeMonitor *, gpointer, gpointer data);
    static void onMountReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onUnmountReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onEjectReady(GObject *source, GAsyncResult *res, gpointer data);

    QList<Place> placesSection() const;
    QList<Place> devicesSection() const;
    QList<Place> bookmarksSection() const;
    QString bookmarkIcon(const QString &location) const;

    // Replace one section's rows with fresh ones, leaving the rest alone.
    void spliceSection(const QString &section, QList<Place> fresh);
    static void releaseRefs(QList<Place> &places);

    QString bookmarksFilePath() const;

    QList<Place> m_places;
    Mounter *m_mounter = nullptr;
    GVolumeMonitor *m_monitor = nullptr;
    QList<gulong> m_monitorHandlers;

    // The Trash row's icon flips to the full-can variant while anything is
    // in the trash, as Nautilus. Kept current by a monitor on trash:///.
    static void onTrashEvent(GFileMonitor *, GFile *, GFile *, gint, gpointer data);
    void refreshTrashState();
    void applyTrashFull(bool full);
    QString trashIcon() const;
    GFileMonitor *m_trashMonitor = nullptr;
    gulong m_trashMonitorHandler = 0;
    QTimer m_trashSettle;
    bool m_trashFull = false;
    QFileSystemWatcher *m_bookmarksWatcher = nullptr;
    QTimer m_devicesSettle;
    QTimer m_bookmarksSettle;
};
