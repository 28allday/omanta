#include "DirectoryModel.h"
#include "Location.h"

#include <QDir>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace {

constexpr int kBatchSize = 256;

// Long enough to swallow the CREATED/CHANGED/CHANGES_DONE burst a single write
// produces, short enough that the view still feels immediate.
constexpr int kSettleMs = 40;

// Set OMANTA_DEBUG_MODEL=1 to trace how the model reacts to disk changes.
// This exists because "updates in place rather than reloading" is a claim that
// is invisible from the outside otherwise — and therefore untestable.
bool modelTracing()
{
    static const bool enabled = qEnvironmentVariableIsSet("OMANTA_DEBUG_MODEL");
    return enabled;
}

void trace(const char *format, const QString &argument)
{
    if (modelTracing()) {
        std::fprintf(stderr, format, qUtf8Printable(argument));
        std::fflush(stderr);
    }
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(kSettleMs);
    connect(&m_settleTimer, &QTimer::timeout, this, &DirectoryModel::flushPending);
}

DirectoryModel::~DirectoryModel()
{
    teardownMonitor();
    cancelInFlight();
    if (m_dir)
        g_object_unref(m_dir);
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const FileEntry &entry = m_entries.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case DisplayNameRole: return entry.displayName;
    case FilePathRole: return Location::child(m_path, entry.name);
    case IsDirRole: return entry.isDir;
    case IsHiddenRole: return entry.isHidden;
    case IsBackupRole: return entry.isBackup;
    case IsSymlinkRole: return entry.isSymlink;
    case SizeRole: return entry.size;
    case ModifiedRole: return entry.modified;
    case CreatedRole: return entry.created;
    case AccessedRole: return entry.accessed;
    case OwnerRole: return entry.owner;
    case GroupRole: return entry.group;
    case PermissionsRole: return entry.permissionString();
    case ContentTypeRole: return entry.contentType;
    case TypeDescriptionRole: return entry.typeDescription;
    case IconSourceRole:
        // The provider takes the whole candidate list and falls back through it.
        return QStringLiteral("image://fileicon/") + entry.iconNames.join(QLatin1Char(','));
    case OrigPathRole: return entry.origPath;
    case TargetPathRole: return entry.targetPath;
    case ItemCountRole: return entry.itemCount;
    case ItemCountAllRole: return entry.itemCountAll;
    // Tree roles so one delegate serves flat and tree models alike; only
    // DirectoryTreeModel ever answers anything else.
    case DepthRole: return 0;
    case ExpandedRole: return false;
    default: return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { DisplayNameRole, "displayName" },
        { FilePathRole, "filePath" },
        { IsDirRole, "isDir" },
        { IsHiddenRole, "isHidden" },
        { IsBackupRole, "isBackup" },
        { IsSymlinkRole, "isSymlink" },
        { SizeRole, "size" },
        { ModifiedRole, "modified" },
        { CreatedRole, "created" },
        { AccessedRole, "accessed" },
        { OwnerRole, "owner" },
        { GroupRole, "group" },
        { PermissionsRole, "permissions" },
        { ContentTypeRole, "contentType" },
        { TypeDescriptionRole, "typeDescription" },
        { IconSourceRole, "iconSource" },
        { OrigPathRole, "origPath" },
        { TargetPathRole, "targetPath" },
        { ItemCountRole, "itemCount" },
        { ItemCountAllRole, "itemCountAll" },
        { DepthRole, "depth" },
        { ExpandedRole, "expanded" },
    };
}

QString DirectoryModel::filePathAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return Location::child(m_path, m_entries.at(row).name);
}

int DirectoryModel::indexOfName(const QString &name) const
{
    return m_rowByName.value(name, -1);
}

QStringList DirectoryModel::allNames() const
{
    QStringList names;
    names.reserve(m_entries.size());
    for (const FileEntry &entry : m_entries)
        names << entry.name;
    return names;
}

void DirectoryModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void DirectoryModel::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}

void DirectoryModel::cancelInFlight()
{
    if (!m_cancellable)
        return;
    g_cancellable_cancel(m_cancellable);
    g_object_unref(m_cancellable);
    m_cancellable = nullptr;
}

void DirectoryModel::teardownMonitor()
{
    if (!m_monitor)
        return;
    if (m_monitorHandler) {
        g_signal_handler_disconnect(m_monitor, m_monitorHandler);
        m_monitorHandler = 0;
    }
    g_file_monitor_cancel(m_monitor);
    g_object_unref(m_monitor);
    m_monitor = nullptr;
}

void DirectoryModel::setPath(const QString &path)
{
    const QString clean = Location::clean(path);
    if (clean == m_path)
        return;

    m_path = clean;
    Q_EMIT pathChanged();
    beginLoad();
}

void DirectoryModel::reload()
{
    beginLoad();
}

void DirectoryModel::beginLoad()
{
    ++m_generation;
    cancelInFlight();
    teardownMonitor();
    m_settleTimer.stop();
    m_pendingNames.clear();
    // In-flight count callbacks die on the generation guard; the running flag
    // belongs to the new load from here on.
    m_countQueue.clear();
    m_countQueued.clear();
    m_countRunning = false;
    setErrorMessage({});

    beginResetModel();
    m_entries.clear();
    m_rowByName.clear();
    endResetModel();
    Q_EMIT countChanged();

    if (m_path.isEmpty()) {
        setLoading(false);
        return;
    }

    setLoading(true);
    trace("[model] full-load %s\n", m_path);

    if (m_dir)
        g_object_unref(m_dir);
    m_dir = Location::make(m_path);

    m_cancellable = g_cancellable_new();

    auto *ctx = new CallbackCtx{ this, m_generation, {} };
    g_file_enumerate_children_async(m_dir,
                                    FileEntry::queryAttributes(),
                                    G_FILE_QUERY_INFO_NONE,
                                    G_PRIORITY_DEFAULT,
                                    m_cancellable,
                                    &DirectoryModel::onEnumerateReady,
                                    ctx);

    armMonitor(m_dir);
}

void DirectoryModel::armMonitor(GFile *dir)
{
    GError *error = nullptr;
    // WATCH_MOVES turns a rename into one RENAMED event instead of a
    // DELETE/CREATE pair, which is what lets selection survive a rename.
    m_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, nullptr, &error);
    if (m_monitor) {
        m_monitorHandler = g_signal_connect(m_monitor, "changed",
                                            G_CALLBACK(&DirectoryModel::onMonitorChanged), this);
    } else {
        qWarning("omanta: could not watch %s: %s", qUtf8Printable(m_path),
                 error ? error->message : "unknown");
        g_clear_error(&error);
    }
}

void DirectoryModel::onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CallbackCtx *>(data);

    GError *error = nullptr;
    GFileEnumerator *enumerator = g_file_enumerate_children_finish(G_FILE(source), res, &error);

    DirectoryModel *model = ctx->model;
    if (!model || ctx->generation != model->m_generation) {
        if (enumerator)
            g_object_unref(enumerator);
        g_clear_error(&error);
        delete ctx;
        return;
    }

    if (!enumerator) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
            model->setErrorMessage(QStringLiteral("Mounting…"));
            Q_EMIT model->needsMount(model->m_path);
        } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            model->setErrorMessage(QString::fromUtf8(error ? error->message : "Could not open folder"));
        }
        g_clear_error(&error);
        model->setLoading(false);
        delete ctx;
        return;
    }

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_DEFAULT,
                                       model->m_cancellable,
                                       &DirectoryModel::onNextFilesReady, ctx);
}

void DirectoryModel::onNextFilesReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CallbackCtx *>(data);
    auto *enumerator = G_FILE_ENUMERATOR(source);

    GError *error = nullptr;
    GList *infos = g_file_enumerator_next_files_finish(enumerator, res, &error);

    DirectoryModel *model = ctx->model;
    const bool stale = !model || ctx->generation != model->m_generation;
    const bool done = stale || error || !infos;

    if (done) {
        if (model && !stale) {
            if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                model->setErrorMessage(QString::fromUtf8(error->message));
            model->setLoading(false);
            model->enqueueAllCounts();
        }
        g_clear_error(&error);
        if (infos)
            g_list_free_full(infos, g_object_unref);
        g_file_enumerator_close_async(enumerator, G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
        g_object_unref(enumerator);
        delete ctx;
        return;
    }

    model->appendBatch(infos);
    g_list_free_full(infos, g_object_unref);

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_DEFAULT,
                                       model->m_cancellable,
                                       &DirectoryModel::onNextFilesReady, ctx);
}

void DirectoryModel::appendBatch(GList *infos)
{
    QList<FileEntry> batch;
    batch.reserve(kBatchSize);
    for (GList *node = infos; node; node = node->next)
        batch.append(FileEntry::fromInfo(G_FILE_INFO(node->data)));

    if (batch.isEmpty())
        return;

    const int first = int(m_entries.size());
    beginInsertRows({}, first, first + int(batch.size()) - 1);
    for (int i = 0; i < batch.size(); ++i)
        m_rowByName.insert(batch.at(i).name, first + i);
    m_entries.append(batch);
    endInsertRows();

    Q_EMIT countChanged();
}

void DirectoryModel::applyBatch(RefreshBatch *batch)
{
    // Removals first: applying them before the inserts keeps the row numbers
    // used for insertion correct without a second pass.
    removeNames(batch->gone);
    applyEntries(batch->arrived);

    // A refreshed folder arrives with unknown counts (fromInfo never fills
    // them), which is the desired outcome: the event that triggered the
    // refresh may well have been its contents changing. Count it again.
    for (const FileEntry &entry : std::as_const(batch->arrived)) {
        if (entry.isDir)
            enqueueCount(entry.name);
    }
}

void DirectoryModel::applyEntries(const QList<FileEntry> &entries)
{
    QList<FileEntry> inserts;
    inserts.reserve(entries.size());

    for (const FileEntry &entry : entries) {
        const int existing = m_rowByName.value(entry.name, -1);
        if (existing >= 0) {
            m_entries[existing] = entry;
            trace("[model] update %s\n", entry.name);
            const QModelIndex idx = index(existing);
            Q_EMIT dataChanged(idx, idx);
        } else {
            trace("[model] insert %s\n", entry.name);
            inserts.append(entry);
        }
    }

    if (inserts.isEmpty())
        return;

    // Every new file in this batch arrives as a single insertion, so the view
    // and the sort proxy do one round of work rather than one per file.
    const int first = int(m_entries.size());
    beginInsertRows({}, first, first + int(inserts.size()) - 1);
    for (int i = 0; i < inserts.size(); ++i)
        m_rowByName.insert(inserts.at(i).name, first + i);
    m_entries.append(inserts);
    endInsertRows();

    Q_EMIT countChanged();
}

void DirectoryModel::removeNames(const QStringList &names)
{
    QList<int> rows;
    rows.reserve(names.size());
    for (const QString &name : names) {
        const int row = m_rowByName.value(name, -1);
        if (row < 0)
            continue;
        trace("[model] remove %s\n", name);
        rows.append(row);
    }

    if (rows.isEmpty())
        return;

    // Removing from the bottom up keeps the lower indices valid, and adjacent
    // rows are removed as one span — deleting a whole folder's contents is a
    // handful of signals rather than one per file.
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    int i = 0;
    while (i < rows.size()) {
        const int last = rows.at(i);
        int first = last;
        int j = i;
        while (j + 1 < rows.size() && rows.at(j + 1) == first - 1) {
            --first;
            ++j;
        }

        beginRemoveRows({}, first, last);
        m_entries.remove(first, last - first + 1);
        endRemoveRows();

        i = j + 1;
    }

    // Rebuilt once for the whole batch. Doing it per removal made deleting n
    // files O(n^2).
    rebuildIndex();
    Q_EMIT countChanged();
}

void DirectoryModel::rebuildIndex()
{
    m_rowByName.clear();
    m_rowByName.reserve(int(m_entries.size()));
    for (int i = 0; i < m_entries.size(); ++i)
        m_rowByName.insert(m_entries.at(i).name, i);
}

void DirectoryModel::queueRefresh(const QString &name)
{
    if (name.isEmpty())
        return;
    m_pendingNames.insert(name);
    m_settleTimer.start();
}

void DirectoryModel::flushPending()
{
    if (!m_dir)
        return;

    const QSet<QString> names = std::exchange(m_pendingNames, {});
    if (names.isEmpty())
        return;

    trace("[model] flush %s\n", QString::number(names.size()));

    // One query per changed name. Whether the file was created, modified or
    // deleted doesn't matter: if the query succeeds we take the fresh info, and
    // if it comes back NOT_FOUND we drop the row. That makes the refresh
    // idempotent, so a burst of contradictory events still converges on truth.
    auto *batch = new RefreshBatch{ this, m_generation, int(names.size()), {}, {} };

    for (const QString &name : names) {
        GFile *child = g_file_get_child(m_dir, name.toUtf8().constData());
        auto *ctx = new RefreshCtx{ batch, name };
        g_file_query_info_async(child,
                                FileEntry::queryAttributes(),
                                G_FILE_QUERY_INFO_NONE,
                                G_PRIORITY_DEFAULT,
                                m_cancellable,
                                &DirectoryModel::onRefreshInfoReady,
                                ctx);
        g_object_unref(child);
    }
}

void DirectoryModel::onRefreshInfoReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<RefreshCtx *>(data);
    RefreshBatch *batch = ctx->batch;

    GError *error = nullptr;
    GFileInfo *info = g_file_query_info_finish(G_FILE(source), res, &error);

    if (info) {
        batch->arrived.append(FileEntry::fromInfo(info));
        g_object_unref(info);
    } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        batch->gone.append(ctx->name);
    }

    g_clear_error(&error);
    delete ctx;

    // The batch is applied — and freed — by whichever query finishes last.
    if (--batch->outstanding > 0)
        return;

    if (batch->model && batch->generation == batch->model->m_generation)
        batch->model->applyBatch(batch);

    delete batch;
}

void DirectoryModel::setCountItems(bool enabled)
{
    if (m_countItems == enabled)
        return;
    m_countItems = enabled;
    Q_EMIT countItemsChanged();

    if (enabled) {
        enqueueAllCounts();
        return;
    }

    // Off: forget the queue and revert every folder to unknown, so display
    // and sorting stop claiming numbers the preference just disavowed. An
    // in-flight count lands harmlessly — finishCount checks the flag.
    m_countQueue.clear();
    m_countQueued.clear();
    bool any = false;
    for (FileEntry &entry : m_entries) {
        if (entry.itemCount >= 0 || entry.itemCountAll >= 0) {
            entry.itemCount = -1;
            entry.itemCountAll = -1;
            any = true;
        }
    }
    if (any && !m_entries.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_entries.size()) - 1));
}

void DirectoryModel::enqueueCount(const QString &name)
{
    if (!m_countItems || name.isEmpty() || m_countQueued.contains(name))
        return;
    m_countQueue.append(name);
    m_countQueued.insert(name);
    pumpCounts();
}

void DirectoryModel::enqueueAllCounts()
{
    if (!m_countItems)
        return;
    for (const FileEntry &entry : std::as_const(m_entries)) {
        if (entry.isDir && entry.itemCount < 0)
            enqueueCount(entry.name);
    }
}

void DirectoryModel::pumpCounts()
{
    if (m_countRunning || !m_dir || !m_cancellable)
        return;

    QString name;
    while (!m_countQueue.isEmpty()) {
        name = m_countQueue.takeFirst();
        m_countQueued.remove(name);
        if (m_rowByName.value(name, -1) >= 0)
            break; // still listed — count it
        name.clear(); // row vanished while queued
    }
    if (name.isEmpty())
        return;

    m_countRunning = true;
    GFile *child = g_file_get_child(m_dir, name.toUtf8().constData());
    auto *ctx = new CountCtx{ this, m_generation, name, 0, 0 };
    // The same three attributes Nautilus counts with: the visible tally
    // skips hidden and backup children, the full tally takes everything.
    g_file_enumerate_children_async(child,
                                    G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
                                    G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP,
                                    G_FILE_QUERY_INFO_NONE,
                                    G_PRIORITY_LOW, // listings and refreshes first
                                    m_cancellable,
                                    &DirectoryModel::onCountEnumerateReady,
                                    ctx);
    g_object_unref(child);
}

void DirectoryModel::onCountEnumerateReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CountCtx *>(data);

    GError *error = nullptr;
    GFileEnumerator *enumerator = g_file_enumerate_children_finish(G_FILE(source), res, &error);

    DirectoryModel *model = ctx->model;
    if (!model || ctx->generation != model->m_generation) {
        if (enumerator)
            g_object_unref(enumerator);
        g_clear_error(&error);
        delete ctx;
        return;
    }

    if (!enumerator) {
        // Unreadable folder: the count stays unknown, exactly what -1 means.
        g_clear_error(&error);
        model->m_countRunning = false;
        model->pumpCounts();
        delete ctx;
        return;
    }

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_LOW,
                                       model->m_cancellable,
                                       &DirectoryModel::onCountNextReady, ctx);
}

void DirectoryModel::onCountNextReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CountCtx *>(data);
    auto *enumerator = G_FILE_ENUMERATOR(source);

    GError *error = nullptr;
    GList *infos = g_file_enumerator_next_files_finish(enumerator, res, &error);

    DirectoryModel *model = ctx->model;
    const bool stale = !model || ctx->generation != model->m_generation;

    if (stale || error || !infos) {
        if (model && !stale) {
            if (!error) // a failed walk must not report a half-count
                model->finishCount(ctx->name, ctx->visible, ctx->total);
            model->m_countRunning = false;
            model->pumpCounts();
        }
        g_clear_error(&error);
        if (infos)
            g_list_free_full(infos, g_object_unref);
        g_file_enumerator_close_async(enumerator, G_PRIORITY_DEFAULT, nullptr, nullptr, nullptr);
        g_object_unref(enumerator);
        delete ctx;
        return;
    }

    for (GList *node = infos; node; node = node->next) {
        auto *info = G_FILE_INFO(node->data);
        ++ctx->total;
        if (!g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN)
            && !g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP))
            ++ctx->visible;
    }
    g_list_free_full(infos, g_object_unref);

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_LOW,
                                       model->m_cancellable,
                                       &DirectoryModel::onCountNextReady, ctx);
}

void DirectoryModel::finishCount(const QString &name, int visible, int total)
{
    if (!m_countItems)
        return; // switched off while the walk ran
    const int row = m_rowByName.value(name, -1);
    if (row < 0 || !m_entries.at(row).isDir)
        return;
    m_entries[row].itemCount = visible;
    m_entries[row].itemCountAll = total;
    trace("[model] count %s\n", name);
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx);
}

void DirectoryModel::onMonitorChanged(GFileMonitor *, GFile *file, GFile *other,
                                      GFileMonitorEvent event, gpointer data)
{
    auto *model = static_cast<DirectoryModel *>(data);

    auto basenameOf = [](GFile *f) -> QString {
        if (!f)
            return {};
        char *base = g_file_get_basename(f);
        QString result = QString::fromUtf8(base ? base : "");
        g_free(base);
        return result;
    };

    switch (event) {
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
        model->queueRefresh(basenameOf(file));
        break;
    case G_FILE_MONITOR_EVENT_RENAMED:
        // Both ends change: the old name disappears, the new one appears.
        model->queueRefresh(basenameOf(file));
        model->queueRefresh(basenameOf(other));
        break;
    case G_FILE_MONITOR_EVENT_UNMOUNTED:
    case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
        break;
    default:
        break;
    }
}
