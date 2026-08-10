#include "StarredModel.h"
#include "DirectoryModel.h"
#include "Location.h"
#include "StarredStore.h"

#include <QFileInfo>

StarredModel::StarredModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

StarredModel::~StarredModel()
{
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
    }
}

void StarredModel::setStore(StarredStore *store)
{
    if (m_store == store)
        return;
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store)
        connect(m_store, &StarredStore::changed, this, [this] {
            if (m_active)
                reload();
        });
    Q_EMIT storeChanged();
    if (m_active)
        reload();
}

void StarredModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    if (m_active)
        reload();
}

void StarredModel::reload()
{
    if (!m_store)
        return;

    ++m_generation;
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
    }
    m_cancellable = g_cancellable_new();

    const QStringList paths = m_store->paths();
    m_loading.clear();
    m_loading.resize(paths.size());
    m_outstanding = int(paths.size());

    if (m_outstanding == 0) {
        finishLoad();
        return;
    }

    for (int i = 0; i < paths.size(); ++i) {
        m_loading[i].path = paths.at(i);
        GFile *file = Location::make(paths.at(i));
        auto *pending = new Pending{ this, m_generation, i };
        g_file_query_info_async(file, FileEntry::queryAttributes(), G_FILE_QUERY_INFO_NONE,
                                G_PRIORITY_DEFAULT, m_cancellable,
                                &StarredModel::onInfoReady, pending);
        g_object_unref(file);
    }
}

void StarredModel::onInfoReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *pending = static_cast<Pending *>(data);
    GFileInfo *info = g_file_query_info_finish(G_FILE(source), res, nullptr);

    StarredModel *model = pending->model.data();
    if (!model || pending->generation != model->m_generation) {
        g_clear_object(&info);
        delete pending;
        return;
    }

    if (info) {
        // A star whose file has vanished is silently dropped from the view —
        // the star itself stays, so the row comes back if the file does.
        model->m_loading[pending->index].entry = FileEntry::fromInfo(info);
        g_object_unref(info);
    }

    if (--model->m_outstanding == 0)
        model->finishLoad();
    delete pending;
}

void StarredModel::finishLoad()
{
    // Wholesale reset, deliberately unlike DirectoryModel: the starred list
    // is small and changes only on star/unstar, not under a file monitor.
    beginResetModel();
    m_rows.clear();
    for (const Row &row : std::as_const(m_loading)) {
        if (!row.entry.name.isEmpty())
            m_rows.append(row);
    }
    m_loading.clear();
    endResetModel();
    Q_EMIT countChanged();
}

int StarredModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant StarredModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());

    switch (role) {
    case DirectoryModel::NameRole: return row.path;
    case DirectoryModel::DisplayNameRole: return row.entry.displayName;
    case DirectoryModel::FilePathRole: return row.path;
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

QHash<int, QByteArray> StarredModel::roleNames() const
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
