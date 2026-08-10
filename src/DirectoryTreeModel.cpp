#include "DirectoryTreeModel.h"

DirectoryTreeModel::DirectoryTreeModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

DirectoryTreeModel::~DirectoryTreeModel()
{
    teardown();
}

int DirectoryTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_flat.size());
}

QVariant DirectoryTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_flat.size())
        return {};

    const FlatRow &entry = m_flat.at(index.row());
    switch (role) {
    // The name is the identity selection is keyed by — relative to the tab
    // root so it stays unique across levels, and identical to the plain name
    // at depth 0 so flat-mode selections survive a mode switch.
    case DirectoryModel::NameRole: return entry.rel;
    case DirectoryModel::DepthRole: return entry.node->depth;
    case DirectoryModel::ExpandedRole: return m_nodes.contains(entry.rel);
    default:
        return entry.node->proxy->data(entry.node->proxy->index(entry.row, 0), role);
    }
}

QHash<int, QByteArray> DirectoryTreeModel::roleNames() const
{
    // Byte-for-byte the DirectoryModel names — the delegates must not be able
    // to tell which model is underneath.
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
        { DirectoryModel::CreatedRole, "created" },
        { DirectoryModel::AccessedRole, "accessed" },
        { DirectoryModel::OwnerRole, "owner" },
        { DirectoryModel::GroupRole, "group" },
        { DirectoryModel::PermissionsRole, "permissions" },
        { DirectoryModel::ContentTypeRole, "contentType" },
        { DirectoryModel::TypeDescriptionRole, "typeDescription" },
        { DirectoryModel::IconSourceRole, "iconSource" },
        { DirectoryModel::OrigPathRole, "origPath" },
        { DirectoryModel::TargetPathRole, "targetPath" },
        { DirectoryModel::ItemCountRole, "itemCount" },
        { DirectoryModel::ItemCountAllRole, "itemCountAll" },
        { DirectoryModel::DepthRole, "depth" },
        { DirectoryModel::ExpandedRole, "expanded" },
    };
}

QString DirectoryTreeModel::relFor(const TreeNode *node, int proxyRow) const
{
    const QString name = node->proxy->data(node->proxy->index(proxyRow, 0),
                                           DirectoryModel::NameRole).toString();
    return node->relPath.isEmpty() ? name : node->relPath + QLatin1Char('/') + name;
}

void DirectoryTreeModel::appendVisible(TreeNode *node, QList<FlatRow> &out) const
{
    const int rows = node->proxy->rowCount();
    for (int r = 0; r < rows; ++r) {
        const QString rel = relFor(node, r);
        out.append({ node, r, rel });
        // A child node only renders under a row its parent's filter accepts,
        // so hiding a folder hides its expanded descendants with it.
        if (TreeNode *child = m_nodes.value(rel))
            appendVisible(child, out);
    }
}

void DirectoryTreeModel::rebuildIndexes()
{
    m_rowByName.clear();
    m_rowByName.reserve(int(m_flat.size()));
    m_flatByNodeRow.clear();
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it)
        m_flatByNodeRow[it.value()] = QList<int>(it.value()->proxy->rowCount(), -1);
    for (int i = 0; i < m_flat.size(); ++i) {
        const FlatRow &entry = m_flat.at(i);
        m_rowByName.insert(entry.rel, i);
        auto listIt = m_flatByNodeRow.find(entry.node);
        if (listIt != m_flatByNodeRow.end() && entry.row < listIt->size())
            (*listIt)[entry.row] = i;
    }
}

void DirectoryTreeModel::structuralSync()
{
    if (m_syncing)
        return;
    m_syncing = true;

    QList<FlatRow> fresh;
    if (m_root)
        appendVisible(m_root, fresh);

    const auto same = [](const FlatRow &a, const FlatRow &b) {
        return a.node == b.node && a.rel == b.rel;
    };

    const int oldCount = int(m_flat.size());
    const int newCount = int(fresh.size());
    int prefix = 0;
    while (prefix < oldCount && prefix < newCount && same(m_flat.at(prefix), fresh.at(prefix)))
        ++prefix;
    int oldSuffix = oldCount;
    int newSuffix = newCount;
    while (oldSuffix > prefix && newSuffix > prefix
           && same(m_flat.at(oldSuffix - 1), fresh.at(newSuffix - 1))) {
        --oldSuffix;
        --newSuffix;
    }

    if (oldCount == newCount && prefix == oldCount) {
        // Identical identities; proxy row numbers may still have shifted.
        m_flat = fresh;
        rebuildIndexes();
    } else if (newCount > oldCount && oldSuffix == prefix) {
        // One contiguous run of new rows — the common case (a load batch, a
        // paste, an expansion filling in) — lands as a plain insert and the
        // view keeps its scroll position.
        beginInsertRows({}, prefix, prefix + (newCount - oldCount) - 1);
        m_flat = fresh;
        rebuildIndexes();
        endInsertRows();
        Q_EMIT countChanged();
    } else if (newCount < oldCount && newSuffix == prefix) {
        beginRemoveRows({}, prefix, prefix + (oldCount - newCount) - 1);
        m_flat = fresh;
        rebuildIndexes();
        endRemoveRows();
        Q_EMIT countChanged();
    } else if (oldCount == newCount) {
        // Same rows, new order — a re-sort. Travels as a layout change so the
        // view refreshes in place rather than jumping to the top.
        Q_EMIT layoutAboutToBeChanged();
        m_flat = fresh;
        rebuildIndexes();
        Q_EMIT layoutChanged();
    } else {
        // A compound change nothing narrower can express. Rare — several
        // monitors bursting in the same tick — and honesty beats a wrong diff.
        beginResetModel();
        m_flat = fresh;
        rebuildIndexes();
        endResetModel();
        Q_EMIT countChanged();
    }

    // A node whose folder row vanished (deleted or renamed away) renders
    // nothing but would keep its monitor alive forever. Prune it — and its
    // descendants — once the rows are settled.
    const QStringList keys = m_nodes.keys();
    for (const QString &key : keys) {
        if (!key.isEmpty() && m_nodes.contains(key) && !m_rowByName.contains(key))
            destroySubtree(key);
    }

    m_syncing = false;
}

void DirectoryTreeModel::forwardDataChanged(TreeNode *node, int first, int last,
                                            const QList<int> &roles)
{
    const QList<int> map = m_flatByNodeRow.value(node);
    for (int r = first; r <= last; ++r) {
        if (r < 0 || r >= map.size())
            continue;
        const int flat = map.at(r);
        if (flat < 0 || flat >= m_flat.size())
            continue;

        const QString rel = relFor(node, r);
        FlatRow &entry = m_flat[flat];
        if (entry.rel != rel) {
            // A rename. If the old name anchored an expanded subtree, its
            // children's identities are all stale — collapse it rather than
            // advertise rows whose paths no longer exist.
            if (m_nodes.contains(entry.rel)) {
                destroySubtree(entry.rel);
                structuralSync();
                return;
            }
            m_rowByName.remove(entry.rel);
            entry.rel = rel;
            m_rowByName.insert(rel, flat);
        }

        const QModelIndex idx = index(flat);
        Q_EMIT dataChanged(idx, idx, roles);
    }
}

void DirectoryTreeModel::connectNode(TreeNode *node)
{
    FileSortFilterModel *proxy = node->proxy;
    const auto sync = [this] { structuralSync(); };
    connect(proxy, &QAbstractItemModel::rowsInserted, this, sync);
    connect(proxy, &QAbstractItemModel::rowsRemoved, this, sync);
    connect(proxy, &QAbstractItemModel::modelReset, this, sync);
    connect(proxy, &QAbstractItemModel::layoutChanged, this, sync);
    connect(proxy, &QAbstractItemModel::dataChanged, this,
            [this, node](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                         const QList<int> &roles) {
                forwardDataChanged(node, topLeft.row(), bottomRight.row(), roles);
            });
}

void DirectoryTreeModel::applySortTo(FileSortFilterModel *proxy) const
{
    proxy->setSortKey(static_cast<FileSortFilterModel::SortKey>(m_sortKey));
    proxy->setSortDescending(m_sortDescending);
    proxy->setFoldersFirst(m_foldersFirst);
    proxy->setShowHidden(m_showHidden);
}

void DirectoryTreeModel::destroySubtree(const QString &relPath)
{
    const QString prefix = relPath + QLatin1Char('/');
    const QStringList keys = m_nodes.keys();
    for (const QString &key : keys) {
        if (key != relPath && !key.startsWith(prefix))
            continue;
        TreeNode *node = m_nodes.take(key);
        disconnect(node->proxy, nullptr, this, nullptr);
        node->proxy->deleteLater();
        if (node->ownsModel)
            node->model->deleteLater();
        delete node;
    }
}

void DirectoryTreeModel::clearChildNodes()
{
    const QStringList keys = m_nodes.keys();
    for (const QString &key : keys) {
        if (key.isEmpty())
            continue; // the root stays
        TreeNode *node = m_nodes.take(key);
        disconnect(node->proxy, nullptr, this, nullptr);
        node->proxy->deleteLater();
        if (node->ownsModel)
            node->model->deleteLater();
        delete node;
    }
}

void DirectoryTreeModel::teardown()
{
    clearChildNodes();
    if (m_root) {
        m_nodes.remove(QString());
        disconnect(m_root->proxy, nullptr, this, nullptr);
        delete m_root->proxy;
        m_root->proxy = nullptr;
        delete m_root;
        m_root = nullptr;
    }
    m_flat.clear();
    m_rowByName.clear();
    m_flatByNodeRow.clear();
}

void DirectoryTreeModel::setRootModel(DirectoryModel *model)
{
    if (m_rootModel == model)
        return;

    beginResetModel();
    teardown();
    if (m_rootModel)
        disconnect(m_rootModel, nullptr, this, nullptr);
    m_rootModel = model;

    if (m_rootModel) {
        m_root = new TreeNode{ QString(), 0, m_rootModel, new FileSortFilterModel(this), false };
        m_root->proxy->setSourceModel(m_rootModel);
        applySortTo(m_root->proxy);
        connectNode(m_root);
        m_nodes.insert(QString(), m_root);
        // Navigating the root is a new tree: every expansion belongs to the
        // old location. pathChanged fires before the reload's reset, so the
        // stale nodes are gone before their parent rows are.
        connect(m_rootModel, &DirectoryModel::pathChanged, this, [this] {
            clearChildNodes();
            structuralSync();
        });
        connect(m_rootModel, &DirectoryModel::countItemsChanged, this, [this] {
            for (TreeNode *node : std::as_const(m_nodes)) {
                if (node->ownsModel)
                    node->model->setCountItems(m_rootModel->countItems());
            }
        });
        appendVisible(m_root, m_flat);
    }
    rebuildIndexes();
    endResetModel();
    Q_EMIT rootModelChanged();
    Q_EMIT countChanged();
}

void DirectoryTreeModel::expand(int row)
{
    if (row < 0 || row >= m_flat.size())
        return;
    const FlatRow entry = m_flat.at(row);
    if (!entry.node->proxy->data(entry.node->proxy->index(entry.row, 0),
                                 DirectoryModel::IsDirRole).toBool())
        return;
    if (m_nodes.contains(entry.rel))
        return;

    const QString location = entry.node->proxy->data(entry.node->proxy->index(entry.row, 0),
                                                     DirectoryModel::FilePathRole).toString();
    auto *node = new TreeNode{ entry.rel, entry.node->depth + 1,
                               new DirectoryModel(this), new FileSortFilterModel(this), true };
    node->proxy->setSourceModel(node->model);
    applySortTo(node->proxy);
    connectNode(node);
    node->model->setCountItems(m_rootModel && m_rootModel->countItems());
    m_nodes.insert(entry.rel, node);
    // The listing arrives asynchronously; rows appear via the node's inserts.
    node->model->setPath(location);

    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, { DirectoryModel::ExpandedRole });
}

void DirectoryTreeModel::collapse(int row)
{
    if (row < 0 || row >= m_flat.size())
        return;
    const QString rel = m_flat.at(row).rel;
    if (!m_nodes.contains(rel) || rel.isEmpty())
        return;

    destroySubtree(rel);
    structuralSync();

    const int stillThere = m_rowByName.value(rel, -1);
    if (stillThere >= 0) {
        const QModelIndex idx = index(stillThere);
        Q_EMIT dataChanged(idx, idx, { DirectoryModel::ExpandedRole });
    }
}

void DirectoryTreeModel::toggleExpanded(int row)
{
    if (row < 0 || row >= m_flat.size())
        return;
    if (m_nodes.contains(m_flat.at(row).rel))
        collapse(row);
    else
        expand(row);
}

void DirectoryTreeModel::setSortKey(int key)
{
    if (m_sortKey == key)
        return;
    m_sortKey = key;
    Q_EMIT sortKeyChanged();
    for (TreeNode *node : std::as_const(m_nodes))
        node->proxy->setSortKey(static_cast<FileSortFilterModel::SortKey>(key));
}

void DirectoryTreeModel::setSortDescending(bool descending)
{
    if (m_sortDescending == descending)
        return;
    m_sortDescending = descending;
    Q_EMIT sortDescendingChanged();
    for (TreeNode *node : std::as_const(m_nodes))
        node->proxy->setSortDescending(descending);
}

void DirectoryTreeModel::setFoldersFirst(bool foldersFirst)
{
    if (m_foldersFirst == foldersFirst)
        return;
    m_foldersFirst = foldersFirst;
    Q_EMIT foldersFirstChanged();
    for (TreeNode *node : std::as_const(m_nodes))
        node->proxy->setFoldersFirst(foldersFirst);
}

void DirectoryTreeModel::setShowHidden(bool showHidden)
{
    if (m_showHidden == showHidden)
        return;
    m_showHidden = showHidden;
    Q_EMIT showHiddenChanged();
    for (TreeNode *node : std::as_const(m_nodes))
        node->proxy->setShowHidden(showHidden);
}

QVariant DirectoryTreeModel::valueAt(int row, const QString &roleName) const
{
    if (row < 0 || row >= m_flat.size())
        return {};
    static thread_local QHash<QString, int> roleIds;
    if (roleIds.isEmpty()) {
        const QHash<int, QByteArray> names = roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            roleIds.insert(QString::fromUtf8(it.value()), it.key());
    }
    const int role = roleIds.value(roleName, -1);
    return role < 0 ? QVariant{} : data(index(row), role);
}

int DirectoryTreeModel::proxyRowForName(const QString &name) const
{
    return m_rowByName.value(name, -1);
}

int DirectoryTreeModel::findByPrefix(const QString &prefix, int startRow) const
{
    const int total = int(m_flat.size());
    if (total == 0 || prefix.isEmpty())
        return -1;
    for (int offset = 0; offset < total; ++offset) {
        const int row = (startRow + offset) % total;
        const QString name = data(index(row), DirectoryModel::DisplayNameRole).toString();
        if (name.startsWith(prefix, Qt::CaseInsensitive))
            return row;
    }
    return -1;
}
