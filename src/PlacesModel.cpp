#include "PlacesModel.h"
#include "Location.h"
#include "Mounter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QUrl>

namespace {

// GIcon → the comma-joined theme-name list IconImageProvider resolves.
QString iconNames(GIcon *icon)
{
    if (G_IS_THEMED_ICON(icon)) {
        const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        QStringList list;
        for (int i = 0; names && names[i]; ++i)
            list.append(QString::fromUtf8(names[i]));
        if (!list.isEmpty())
            return list.join(QLatin1Char(','));
    }
    return QStringLiteral("folder");
}

QString iconUrl(const QString &names)
{
    return QStringLiteral("image://fileicon/") + names;
}

struct MountCtx {
    QPointer<PlacesModel> model;
    QString name;
    GVolume *volume = nullptr;
    GMount *mount = nullptr;
};

} // namespace

PlacesModel::PlacesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // The monitor must exist before the first devicesSection() call below —
    // getting this backwards leaves Devices permanently empty, with only a
    // GLib CRITICAL on stderr to say why.
    m_monitor = g_volume_monitor_get();

    // The exact order Nautilus lays its sidebar out in: the fixed places,
    // then the GTK bookmarks, then devices.
    m_places = placesSection();
    m_places += bookmarksSection();
    m_places += devicesSection();

    // Devices: every add/remove/change on volumes, mounts or drives re-derives
    // the section. Events arrive in bursts (plugging a stick fires several);
    // settle briefly and derive once.
    m_devicesSettle.setSingleShot(true);
    m_devicesSettle.setInterval(100);
    connect(&m_devicesSettle, &QTimer::timeout, this,
            [this] { spliceSection(QStringLiteral("Devices"), devicesSection()); });
    const char *signals_[] = { "volume-added", "volume-removed", "volume-changed",
                               "mount-added", "mount-removed", "mount-changed",
                               "drive-connected", "drive-disconnected" };
    for (const char *name : signals_) {
        m_monitorHandlers.append(
            g_signal_connect(m_monitor, name, G_CALLBACK(&PlacesModel::onVolumeEvent), this));
    }

    // Bookmarks: one watch on the GTK bookmarks file. Editors replace the
    // file, killing the inode watch — watch the directory too and re-arm.
    m_bookmarksSettle.setSingleShot(true);
    m_bookmarksSettle.setInterval(100);
    connect(&m_bookmarksSettle, &QTimer::timeout, this, [this] {
        spliceSection(QStringLiteral("Bookmarks"), bookmarksSection());
        if (QFile::exists(bookmarksFilePath())
            && !m_bookmarksWatcher->files().contains(bookmarksFilePath()))
            m_bookmarksWatcher->addPath(bookmarksFilePath());
    });

    m_bookmarksWatcher = new QFileSystemWatcher(this);
    if (QFile::exists(bookmarksFilePath()))
        m_bookmarksWatcher->addPath(bookmarksFilePath());
    const QString bookmarksDir = QFileInfo(bookmarksFilePath()).absolutePath();
    if (QDir(bookmarksDir).exists())
        m_bookmarksWatcher->addPath(bookmarksDir);
    connect(m_bookmarksWatcher, &QFileSystemWatcher::fileChanged, this,
            [this] { m_bookmarksSettle.start(); });
    connect(m_bookmarksWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this] { m_bookmarksSettle.start(); });

    // Trash: one monitor keeps the sidebar can honest. Events arrive per
    // trashed file — settle briefly and query the count once.
    m_trashSettle.setSingleShot(true);
    m_trashSettle.setInterval(200);
    connect(&m_trashSettle, &QTimer::timeout, this, &PlacesModel::refreshTrashState);
    GFile *trash = g_file_new_for_uri("trash:///");
    m_trashMonitor = g_file_monitor_directory(trash, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(trash);
    if (m_trashMonitor) {
        m_trashMonitorHandler = g_signal_connect(m_trashMonitor, "changed",
                                                 G_CALLBACK(&PlacesModel::onTrashEvent), this);
    }
    refreshTrashState();
}

PlacesModel::~PlacesModel()
{
    for (gulong handler : std::as_const(m_monitorHandlers))
        g_signal_handler_disconnect(m_monitor, handler);
    g_object_unref(m_monitor);
    if (m_trashMonitor) {
        if (m_trashMonitorHandler)
            g_signal_handler_disconnect(m_trashMonitor, m_trashMonitorHandler);
        g_file_monitor_cancel(m_trashMonitor);
        g_object_unref(m_trashMonitor);
    }
    releaseRefs(m_places);
}

void PlacesModel::releaseRefs(QList<Place> &places)
{
    for (Place &place : places) {
        g_clear_object(&place.volume);
        g_clear_object(&place.mount);
    }
}

int PlacesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_places.size());
}

QVariant PlacesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_places.size())
        return {};
    const Place &place = m_places.at(index.row());

    switch (role) {
    case NameRole: return place.name;
    case LocationRole: return place.location;
    case IconSourceRole: return iconUrl(place.icon);
    case SectionRole: return place.section;
    case MountableRole: return place.mountable;
    case EjectableRole: return place.ejectable;
    }
    return {};
}

QHash<int, QByteArray> PlacesModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { LocationRole, "location" },
        { IconSourceRole, "iconSource" },
        { SectionRole, "section" },
        { MountableRole, "mountable" },
        { EjectableRole, "ejectable" },
    };
}

int PlacesModel::rowForLocation(const QString &location) const
{
    if (location.isEmpty())
        return -1;
    for (int row = 0; row < m_places.size(); ++row) {
        if (m_places.at(row).location == location)
            return row;
    }
    return -1;
}

// ---- trash state ------------------------------------------------------------

QString PlacesModel::trashIcon() const
{
    return m_trashFull
        ? QStringLiteral("user-trash-full-symbolic,user-trash-full")
        : QStringLiteral("user-trash-symbolic,user-trash");
}

void PlacesModel::onTrashEvent(GFileMonitor *, GFile *, GFile *, gint, gpointer data)
{
    static_cast<PlacesModel *>(data)->m_trashSettle.start();
}

void PlacesModel::refreshTrashState()
{
    struct Ctx { QPointer<PlacesModel> model; };
    auto *ctx = new Ctx{ this };

    GFile *trash = g_file_new_for_uri("trash:///");
    g_file_query_info_async(
        trash, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT, G_FILE_QUERY_INFO_NONE,
        G_PRIORITY_LOW, nullptr,
        [](GObject *source, GAsyncResult *result, gpointer data) {
            auto *ctx = static_cast<Ctx *>(data);
            GError *error = nullptr;
            GFileInfo *info = g_file_query_info_finish(G_FILE(source), result, &error);
            if (info) {
                guint32 count = 0;
                if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT))
                    count = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT);
                if (ctx->model)
                    ctx->model->applyTrashFull(count > 0);
                g_object_unref(info);
            }
            g_clear_error(&error);
            delete ctx;
        },
        ctx);
    g_object_unref(trash);
}

void PlacesModel::applyTrashFull(bool full)
{
    if (m_trashFull == full)
        return;
    m_trashFull = full;
    for (qsizetype row = 0; row < m_places.size(); ++row) {
        if (m_places[row].location == QLatin1String("trash:///")) {
            m_places[row].icon = trashIcon();
            const QModelIndex at = index(int(row));
            Q_EMIT dataChanged(at, at, { IconSourceRole });
            break;
        }
    }
}

// ---- sections --------------------------------------------------------------

QList<PlacesModel::Place> PlacesModel::placesSection() const
{
    // Nautilus's fixed rows, in Nautilus's order, and nothing else — the XDG
    // folders it does NOT list here arrive as bookmarks, exactly as it does
    // it. Verified against Files 50.2.2 on the target machine (2026-08-06).
    const QString section = QStringLiteral("Places");
    return {
        { QStringLiteral("Home"), QDir::homePath(),
          QStringLiteral("user-home-symbolic,user-home"), section },
        { QStringLiteral("Recent"), QStringLiteral("recent:///"),
          QStringLiteral("document-open-recent-symbolic,document-open-recent"), section },
        { QStringLiteral("Starred"), QStringLiteral("starred:///"),
          QStringLiteral("starred-symbolic,starred"), section },
        { QStringLiteral("Network"), QStringLiteral("network:///"),
          QStringLiteral("network-workgroup-symbolic,network-workgroup"), section },
        { QStringLiteral("Trash"), QStringLiteral("trash:///"),
          trashIcon(), section },
    };
}

QList<PlacesModel::Place> PlacesModel::devicesSection() const
{
    const QString section = QStringLiteral("Devices");
    QList<Place> places;

    // Volumes first: they cover both mounted and not-yet-mounted devices, and
    // carry the user-facing name ("32 GB Volume") either way.
    GList *volumes = g_volume_monitor_get_volumes(m_monitor);
    for (GList *l = volumes; l; l = l->next) {
        GVolume *volume = G_VOLUME(l->data);

        Place place;
        char *name = g_volume_get_name(volume);
        place.name = QString::fromUtf8(name);
        g_free(name);
        GIcon *icon = g_volume_get_symbolic_icon(volume);
        place.icon = iconNames(icon);
        g_object_unref(icon);
        place.section = section;

        if (GMount *mount = g_volume_get_mount(volume)) {
            GFile *root = g_mount_get_default_location(mount);
            place.location = Location::fromGFile(root);
            g_object_unref(root);
            place.ejectable = g_mount_can_unmount(mount) || g_mount_can_eject(mount);
            place.mount = mount; // keep the ref for eject()
        } else {
            place.mountable = true;
        }
        place.volume = G_VOLUME(g_object_ref(volume));
        places.append(place);
    }
    g_list_free_full(volumes, g_object_unref);

    // Mounts with no backing volume: gvfs shares (smb://, sftp://), MTP.
    GList *mounts = g_volume_monitor_get_mounts(m_monitor);
    for (GList *l = mounts; l; l = l->next) {
        GMount *mount = G_MOUNT(l->data);
        if (g_mount_is_shadowed(mount))
            continue;
        if (GVolume *owner = g_mount_get_volume(mount)) {
            g_object_unref(owner); // already listed through its volume
            continue;
        }

        Place place;
        char *name = g_mount_get_name(mount);
        place.name = QString::fromUtf8(name);
        g_free(name);
        GIcon *icon = g_mount_get_symbolic_icon(mount);
        place.icon = iconNames(icon);
        g_object_unref(icon);
        place.section = section;
        GFile *root = g_mount_get_default_location(mount);
        place.location = Location::fromGFile(root);
        g_object_unref(root);
        place.ejectable = g_mount_can_unmount(mount) || g_mount_can_eject(mount);
        place.mount = G_MOUNT(g_object_ref(mount));
        places.append(place);
    }
    g_list_free_full(mounts, g_object_unref);

    return places;
}

QList<PlacesModel::Place> PlacesModel::bookmarksSection() const
{
    // ~/.config/gtk-3.0/bookmarks: one "file:///path Optional Label" per
    // line. Shared with Nautilus and the GTK file chooser — reading the same
    // file means the same bookmarks everywhere.
    const QString section = QStringLiteral("Bookmarks");
    QList<Place> places;

    QFile file(bookmarksFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return places;

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        const qsizetype space = line.indexOf(QLatin1Char(' '));
        const QString uri = space < 0 ? line : line.left(space);
        const QString label = space < 0 ? QString() : line.mid(space + 1).trimmed();

        const QString location = Location::clean(uri);
        if (location.isEmpty())
            continue;

        places.append({ label.isEmpty() ? Location::displayName(location) : label,
                        location, bookmarkIcon(location), section });
    }
    return places;
}

QString PlacesModel::bookmarkIcon(const QString &location) const
{
    // Nautilus draws each bookmark with its target's icon, so a Downloads
    // bookmark gets the download glyph and a Music one the note — matched by
    // comparing against the XDG special dirs rather than stat'ing anything.
    struct Special {
        GUserDirectory dir;
        const char *icon;
    };
    static const Special specials[] = {
        { G_USER_DIRECTORY_DOCUMENTS, "folder-documents-symbolic,folder-documents" },
        { G_USER_DIRECTORY_DOWNLOAD, "folder-download-symbolic,folder-download" },
        { G_USER_DIRECTORY_MUSIC, "folder-music-symbolic,folder-music" },
        { G_USER_DIRECTORY_PICTURES, "folder-pictures-symbolic,folder-pictures" },
        { G_USER_DIRECTORY_VIDEOS, "folder-videos-symbolic,folder-videos" },
    };
    for (const Special &special : specials) {
        const char *path = g_get_user_special_dir(special.dir);
        if (path && location == QString::fromUtf8(path))
            return QString::fromUtf8(special.icon);
    }
    return QStringLiteral("folder-symbolic,folder");
}

// ---- keeping sections current ----------------------------------------------

void PlacesModel::onVolumeEvent(GVolumeMonitor *, gpointer, gpointer data)
{
    static_cast<PlacesModel *>(data)->m_devicesSettle.start();
}

void PlacesModel::spliceSection(const QString &section, QList<Place> fresh)
{
    // Sections are contiguous; find this one's span. An empty section keeps
    // its position by falling back to where its predecessors end.
    int first = -1;
    int last = -1;
    for (int row = 0; row < m_places.size(); ++row) {
        if (m_places.at(row).section == section) {
            if (first < 0)
                first = row;
            last = row;
        }
    }

    static const QStringList order{ QStringLiteral("Places"), QStringLiteral("Bookmarks"),
                                    QStringLiteral("Devices") };
    if (first < 0) {
        const int rank = int(order.indexOf(section));
        first = 0;
        for (int row = 0; row < m_places.size(); ++row) {
            if (order.indexOf(m_places.at(row).section) < rank)
                first = row + 1;
        }
        last = first - 1;
    }

    if (last >= first) {
        beginRemoveRows({}, first, last);
        QList<Place> removed = m_places.mid(first, last - first + 1);
        m_places.remove(first, last - first + 1);
        endRemoveRows();
        releaseRefs(removed);
    }

    if (!fresh.isEmpty()) {
        beginInsertRows({}, first, first + int(fresh.size()) - 1);
        for (qsizetype i = 0; i < fresh.size(); ++i)
            m_places.insert(first + int(i), fresh.at(i));
        endInsertRows();
    }
    fresh.clear(); // refs now owned by m_places

    Q_EMIT countChanged();
}

// ---- bookmark writes -------------------------------------------------------

namespace {

// The canonical URI for a location — bookmarks compare and store in URI form,
// because that is what GTK writes ("file:///home/user/My%20Films").
QString bookmarkUri(const QString &location)
{
    GFile *file = Location::make(location);
    if (!file)
        return {};
    char *uri = g_file_get_uri(file);
    const QString result = QString::fromUtf8(uri ? uri : "");
    g_free(uri);
    g_object_unref(file);
    return result;
}

} // namespace

bool PlacesModel::isBookmarked(const QString &location) const
{
    const QString cleaned = Location::clean(location);
    if (cleaned.isEmpty())
        return false;

    QFile file(bookmarksFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype space = line.indexOf(QLatin1Char(' '));
        if (Location::clean(space < 0 ? line : line.left(space)) == cleaned)
            return true;
    }
    return false;
}

void PlacesModel::addBookmark(const QString &location)
{
    const QString uri = bookmarkUri(location);
    if (uri.isEmpty() || isBookmarked(location))
        return;

    QDir().mkpath(QFileInfo(bookmarksFilePath()).absolutePath());
    QFile file(bookmarksFilePath());
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text))
        return;

    // Append on a fresh line even if the file doesn't end with one.
    QByteArray contents = file.readAll();
    if (!contents.isEmpty() && !contents.endsWith('\n'))
        contents.append('\n');
    contents.append(uri.toUtf8()).append('\n');
    file.seek(0);
    file.write(contents);
    file.resize(file.pos());
    file.close();

    // The watcher will fire too, but a fresh file was not being watched yet.
    m_bookmarksSettle.start();
}

void PlacesModel::removeBookmark(const QString &location)
{
    const QString cleaned = Location::clean(location);
    if (cleaned.isEmpty())
        return;

    QFile file(bookmarksFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    QStringList kept;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype space = line.indexOf(QLatin1Char(' '));
        if (Location::clean(space < 0 ? line : line.left(space)) == cleaned)
            continue;
        kept.append(line);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    for (const QString &line : std::as_const(kept))
        file.write(line.toUtf8() + '\n');
    file.close();

    m_bookmarksSettle.start();
}

QString PlacesModel::bookmarksFilePath() const
{
    const QString override = qEnvironmentVariable("OMANTA_BOOKMARKS_FILE");
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + QStringLiteral("/.config/gtk-3.0/bookmarks");
}

// ---- mounting and ejecting -------------------------------------------------

void PlacesModel::mount(int row)
{
    if (row < 0 || row >= m_places.size())
        return;
    const Place &place = m_places.at(row);
    if (!place.mountable || !place.volume)
        return;

    auto *ctx = new MountCtx{ this, place.name, G_VOLUME(g_object_ref(place.volume)), nullptr };
    // The operation is borrowed from the window's Mounter, so a volume that
    // wants credentials asks through the same dialog as everything else.
    // Without one (headless tests), credential-free mounts still work.
    GMountOperation *operation = m_mounter ? m_mounter->createOperation() : nullptr;
    g_volume_mount(place.volume, G_MOUNT_MOUNT_NONE, operation, nullptr,
                   &PlacesModel::onMountReady, ctx);
    g_clear_object(&operation);
}

void PlacesModel::onMountReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<MountCtx *>(data);
    GError *error = nullptr;
    const bool ok = g_volume_mount_finish(G_VOLUME(source), res, &error);

    if (ctx->model) {
        if (ok) {
            if (GMount *mount = g_volume_get_mount(ctx->volume)) {
                GFile *root = g_mount_get_default_location(mount);
                Q_EMIT ctx->model->mounted(Location::fromGFile(root));
                g_object_unref(root);
                g_object_unref(mount);
            }
        } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
            Q_EMIT ctx->model->mountFailed(
                ctx->name, error ? QString::fromUtf8(error->message) : QStringLiteral("mount failed"));
        }
    }

    g_clear_error(&error);
    g_object_unref(ctx->volume);
    delete ctx;
}

void PlacesModel::eject(int row)
{
    if (row < 0 || row >= m_places.size())
        return;
    const Place &place = m_places.at(row);

    if (place.mount && g_mount_can_eject(place.mount)) {
        auto *ctx = new MountCtx{ this, place.name, nullptr, G_MOUNT(g_object_ref(place.mount)) };
        g_mount_eject_with_operation(place.mount, G_MOUNT_UNMOUNT_NONE, nullptr, nullptr,
                                     &PlacesModel::onEjectReady, ctx);
    } else if (place.mount && g_mount_can_unmount(place.mount)) {
        auto *ctx = new MountCtx{ this, place.name, nullptr, G_MOUNT(g_object_ref(place.mount)) };
        g_mount_unmount_with_operation(place.mount, G_MOUNT_UNMOUNT_NONE, nullptr, nullptr,
                                       &PlacesModel::onUnmountReady, ctx);
    }
}

void PlacesModel::onUnmountReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<MountCtx *>(data);
    GError *error = nullptr;
    if (!g_mount_unmount_with_operation_finish(G_MOUNT(source), res, &error) && ctx->model
        && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED))
        Q_EMIT ctx->model->mountFailed(ctx->name, QString::fromUtf8(error ? error->message : ""));
    g_clear_error(&error);
    g_object_unref(ctx->mount);
    delete ctx;
}

void PlacesModel::onEjectReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<MountCtx *>(data);
    GError *error = nullptr;
    if (!g_mount_eject_with_operation_finish(G_MOUNT(source), res, &error) && ctx->model
        && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED))
        Q_EMIT ctx->model->mountFailed(ctx->name, QString::fromUtf8(error ? error->message : ""));
    g_clear_error(&error);
    g_object_unref(ctx->mount);
    delete ctx;
}

void PlacesModel::setMounter(Mounter *mounter)
{
    if (m_mounter == mounter)
        return;
    m_mounter = mounter;
    Q_EMIT mounterChanged();
}
