#include "NetworkModel.h"
#include "DirectoryModel.h"
#include "Location.h"

#include <QTimer>
#include <QUrl>

namespace {

// Trailing-slash-insensitive identity, matching ServerStore::normalize —
// the URI a user typed and the URI a mount reports must count as one row.
QString uriKey(const QString &uri)
{
    QString out = uri;
    while (out.endsWith(QLatin1Char('/')) && !out.endsWith(QStringLiteral("://")))
        out.chop(1);
    return out;
}

QStringList themedIconNames(GIcon *icon)
{
    QStringList names;
    if (icon && G_IS_THEMED_ICON(icon)) {
        const gchar *const *list = g_themed_icon_get_names(G_THEMED_ICON(icon));
        for (int i = 0; list && list[i]; ++i)
            names.append(QString::fromUtf8(list[i]));
    }
    return names;
}

} // namespace

NetworkModel::NetworkModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Monitor first, always — asking it for mounts before it exists is the
    // PlacesModel constructor-order gotcha all over again.
    m_monitor = g_volume_monitor_get();
    m_addedHandler = g_signal_connect(m_monitor, "mount-added",
                                      G_CALLBACK(&NetworkModel::onMountsChanged), this);
    m_removedHandler = g_signal_connect(m_monitor, "mount-removed",
                                        G_CALLBACK(&NetworkModel::onMountsChanged), this);
}

NetworkModel::~NetworkModel()
{
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
    }
    if (m_monitor) {
        g_signal_handler_disconnect(m_monitor, m_addedHandler);
        g_signal_handler_disconnect(m_monitor, m_removedHandler);
        g_object_unref(m_monitor);
    }
}

void NetworkModel::onMountsChanged(GVolumeMonitor *, GMount *, gpointer data)
{
    auto *model = static_cast<NetworkModel *>(data);
    // Never reload from inside the monitor's own signal emission.
    QTimer::singleShot(0, model, [model] {
        if (model->m_active)
            model->reload();
    });
}

void NetworkModel::setStore(ServerStore *store)
{
    if (m_store == store)
        return;
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store)
        connect(m_store, &ServerStore::changed, this, [this] {
            if (m_active)
                reload();
        });
    Q_EMIT storeChanged();
    if (m_active)
        reload();
}

void NetworkModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    if (m_active)
        reload();
}

NetworkModel::Row NetworkModel::serverRow(const QString &uri)
{
    Row row;
    row.uri = uri;
    row.entry.name = uri;
    row.entry.isDir = true;
    row.entry.contentType = QStringLiteral("inode/directory");
    row.entry.typeDescription = QStringLiteral("Server");
    row.entry.iconNames = { QStringLiteral("network-server"), QStringLiteral("network-workgroup") };

    // "share on host", Nautilus's reading of an address; a bare host stands
    // alone.
    const QUrl url(uri);
    const QString share = url.fileName();
    row.entry.displayName = share.isEmpty()
        ? (url.host().isEmpty() ? uri : url.host())
        : QStringLiteral("%1 on %2").arg(share, url.host());
    return row;
}

void NetworkModel::appendUnique(QList<Row> &rows, Row row) const
{
    const QString key = uriKey(row.uri);
    for (const Row &existing : rows) {
        if (uriKey(existing.uri) == key)
            return;
    }
    rows.append(std::move(row));
}

QList<NetworkModel::Row> NetworkModel::immediateRows() const
{
    QList<Row> rows;

    if (m_store) {
        const QStringList uris = m_store->uris();
        for (const QString &uri : uris)
            appendUnique(rows, serverRow(uri));
    }

    // Mounted network shares — anything whose root is not a local file.
    GList *mounts = g_volume_monitor_get_mounts(m_monitor);
    for (GList *l = mounts; l; l = l->next) {
        GMount *mount = G_MOUNT(l->data);
        GFile *rootFile = g_mount_get_root(mount);
        gchar *rootUri = g_file_get_uri(rootFile);
        const QString uri = QString::fromUtf8(rootUri);
        g_free(rootUri);
        g_object_unref(rootFile);

        if (!uri.startsWith(QStringLiteral("file://"))) {
            Row row = serverRow(uri);
            gchar *name = g_mount_get_name(mount);
            row.entry.displayName = QString::fromUtf8(name);
            g_free(name);
            GIcon *icon = g_mount_get_icon(mount);
            const QStringList names = themedIconNames(icon);
            if (!names.isEmpty())
                row.entry.iconNames = names;
            g_clear_object(&icon);
            appendUnique(rows, std::move(row));
        }
        g_object_unref(mount);
    }
    g_list_free(mounts);

    return rows;
}

void NetworkModel::reload()
{
    ++m_generation;
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
    }
    m_cancellable = g_cancellable_new();

    // Known servers and mounted shares show immediately; whatever gvfs
    // discovers under network:/// is appended when the enumeration lands —
    // a slow SMB browse must not delay the rows already known.
    m_discovered.clear();
    applyRows();

    GFile *network = g_file_new_for_uri("network:///");
    auto *pending = new Pending{ this, m_generation };
    g_file_enumerate_children_async(
        network,
        (QByteArray(FileEntry::queryAttributes()) + ",standard::target-uri").constData(),
        G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, m_cancellable,
        &NetworkModel::onEnumerateReady, pending);
    g_object_unref(network);
}

void NetworkModel::onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *pending = static_cast<Pending *>(data);
    GFileEnumerator *enumerator = g_file_enumerate_children_finish(G_FILE(source), res, nullptr);

    NetworkModel *model = pending->model.data();
    if (!model || pending->generation != model->m_generation || !enumerator) {
        // No gvfs network backend (or a stale pass) — the immediate rows
        // already shown are the whole answer.
        g_clear_object(&enumerator);
        delete pending;
        return;
    }

    g_file_enumerator_next_files_async(enumerator, 64, G_PRIORITY_DEFAULT,
                                       model->m_cancellable,
                                       &NetworkModel::onNextFiles, pending);
    g_object_unref(enumerator); // next_files holds its own reference via `source`
}

void NetworkModel::onNextFiles(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *pending = static_cast<Pending *>(data);
    GFileEnumerator *enumerator = G_FILE_ENUMERATOR(source);
    GList *infos = g_file_enumerator_next_files_finish(enumerator, res, nullptr);

    NetworkModel *model = pending->model.data();
    if (!model || pending->generation != model->m_generation) {
        g_list_free_full(infos, g_object_unref);
        delete pending;
        return;
    }

    if (!infos) {
        // Enumeration exhausted — the discovered rows are complete.
        model->applyRows();
        delete pending;
        return;
    }

    for (GList *l = infos; l; l = l->next) {
        GFileInfo *info = G_FILE_INFO(l->data);
        Row row;
        row.entry = FileEntry::fromInfo(info);
        // A discovered entry points at its real location (smb://…) through
        // target-uri; the network:///-relative name is useless to navigate.
        const char *target = g_file_info_get_attribute_string(info, "standard::target-uri");
        row.uri = target ? QString::fromUtf8(target)
                         : QStringLiteral("network:///") + row.entry.name;
        row.entry.name = row.uri; // the unique, name-keyed identity
        row.entry.isDir = true;   // every network row opens as a place
        model->m_discovered.append(std::move(row));
    }
    g_list_free_full(infos, g_object_unref);

    g_file_enumerator_next_files_async(enumerator, 64, G_PRIORITY_DEFAULT,
                                       model->m_cancellable,
                                       &NetworkModel::onNextFiles, pending);
}

void NetworkModel::applyRows()
{
    // Wholesale reset, deliberately unlike DirectoryModel: the list is small
    // and rebuilt only on store/mount/discovery changes.
    beginResetModel();
    m_rows = immediateRows();
    for (const Row &row : std::as_const(m_discovered))
        appendUnique(m_rows, row);
    endResetModel();
    Q_EMIT countChanged();
}

int NetworkModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NetworkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());

    switch (role) {
    case DirectoryModel::NameRole: return row.uri;
    case DirectoryModel::DisplayNameRole: return row.entry.displayName;
    case DirectoryModel::FilePathRole: return row.uri;
    case DirectoryModel::IsDirRole: return row.entry.isDir;
    case DirectoryModel::IsHiddenRole: return row.entry.isHidden;
    case DirectoryModel::IsBackupRole: return row.entry.isBackup;
    case DirectoryModel::IsSymlinkRole: return row.entry.isSymlink;
    case DirectoryModel::SizeRole: return row.entry.size;
    case DirectoryModel::ModifiedRole: return row.entry.modified;
    case DirectoryModel::ContentTypeRole: return row.entry.contentType;
    case DirectoryModel::TypeDescriptionRole: return row.entry.typeDescription;
    case DirectoryModel::IconSourceRole:
        return QStringLiteral("image://fileicon/") + row.entry.iconNames.join(QLatin1Char(','));
    case DirectoryModel::OrigPathRole: return row.entry.origPath;
    case DirectoryModel::ItemCountRole: return row.entry.itemCount;
    case DirectoryModel::ItemCountAllRole: return row.entry.itemCountAll;
    case DirectoryModel::DepthRole: return 0;
    case DirectoryModel::ExpandedRole: return false;
    default: return {};
    }
}

QHash<int, QByteArray> NetworkModel::roleNames() const
{
    return {
        { DirectoryModel::NameRole, "name" },
        { DirectoryModel::DisplayNameRole, "displayName" },
        { DirectoryModel::FilePathRole, "filePath" },
        { DirectoryModel::IsDirRole, "isDir" },
        { DirectoryModel::IsHiddenRole, "isHidden" },
        { DirectoryModel::IsBackupRole, "isBackup" },
        { DirectoryModel::IsSymlinkRole, "isSymlink" },
        { DirectoryModel::SizeRole, "size" },
        { DirectoryModel::ModifiedRole, "modified" },
        { DirectoryModel::ContentTypeRole, "contentType" },
        { DirectoryModel::TypeDescriptionRole, "typeDescription" },
        { DirectoryModel::IconSourceRole, "iconSource" },
        { DirectoryModel::OrigPathRole, "origPath" },
        { DirectoryModel::ItemCountRole, "itemCount" },
        { DirectoryModel::ItemCountAllRole, "itemCountAll" },
        { DirectoryModel::DepthRole, "depth" },
        { DirectoryModel::ExpandedRole, "expanded" },
    };
}
