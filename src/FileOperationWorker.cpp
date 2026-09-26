#include "FileOperationWorker.h"
#include "ArchiveEngine.h"
#include "Location.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

namespace {

QString messageOf(GError *error, const char *fallback)
{
    return QString::fromUtf8(error && error->message ? error->message : fallback);
}

qint64 sizeOf(GFile *file)
{
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                        G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
    if (!info)
        return 0;
    const qint64 size = qint64(g_file_info_get_size(info));
    g_object_unref(info);
    return size;
}

bool exists(GFile *file)
{
    return g_file_query_exists(file, nullptr);
}

// NOFOLLOW_SYMLINKS, and it matters: a link to a folder is a *link*, one item
// to copy or delete. Following it here walks somebody else's tree — deleting a
// folder would take the contents of everything it linked to with it.
bool isDirectory(GFile *file)
{
    return g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr)
        == G_FILE_TYPE_DIRECTORY;
}

QString pathOf(GFile *file)
{
    return Location::fromGFile(file);
}

// "notes.txt" → "notes (copy).txt" → "notes (copy 2).txt".
//
// Splitting on the *last* dot but never on a leading one keeps ".bashrc" whole
// instead of producing " (copy).bashrc".
GFile *uniqueChild(GFile *directory, const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const bool hasSuffix = dot > 0;
    const QString stem = hasSuffix ? name.left(dot) : name;
    const QString suffix = hasSuffix ? name.mid(dot) : QString();

    for (int attempt = 1; attempt < 10000; ++attempt) {
        const QString candidate = attempt == 1
            ? QStringLiteral("%1 (copy)%2").arg(stem, suffix)
            : QStringLiteral("%1 (copy %2)%3").arg(stem).arg(attempt).arg(suffix);

        GFile *child = g_file_get_child(directory, candidate.toUtf8().constData());
        if (!exists(child))
            return child;
        g_object_unref(child);
    }
    return nullptr;
}

bool setDirectoryMode(GFile *file, quint32 mode, GCancellable *cancel, GError **error)
{
    GError *modeError = nullptr;
    if (g_file_set_attribute_uint32(file, G_FILE_ATTRIBUTE_UNIX_MODE, mode,
                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &modeError))
        return true;
    // Some remote and removable filesystems have no Unix permission model.
    // Still propagate permission-denied and I/O errors on those that do.
    if (g_error_matches(modeError, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
        g_clear_error(&modeError);
        return true;
    }
    g_propagate_error(error, modeError);
    return false;
}

// Copy only attributes supported by the destination backend. Directory size
// is structural, not writable metadata (GIO includes it in the query list).
bool copyDirectoryMetadata(GFile *source, GFile *destination, GCancellable *cancel,
                           GError **error)
{
    const auto flags = GFileCopyFlags(G_FILE_COPY_ALL_METADATA | G_FILE_COPY_NOFOLLOW_SYMLINKS);
    char *attributes = g_file_build_attribute_list_for_copy(destination, flags, cancel, error);
    if (!attributes)
        return false;
    GFileInfo *info = g_file_query_info(source, attributes, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                       cancel, error);
    g_free(attributes);
    if (!info)
        return false;
    g_file_info_remove_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
    const bool ok = g_file_set_attributes_from_info(destination, info,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    g_object_unref(info);
    return ok;
}

struct ProgressContext {
    FileOperationWorker *worker;
    quint64 id;
    qint64 doneBefore;
    qint64 total;
    QString currentName;
    QElapsedTimer *throttle;
};

void onCopyProgress(goffset current, goffset, gpointer data)
{
    auto *ctx = static_cast<ProgressContext *>(data);
    // Emitting per callback would flood the main thread's event queue on a big
    // file and make the UI *less* responsive, not more.
    if (ctx->throttle->elapsed() < 100)
        return;
    ctx->throttle->restart();
    Q_EMIT ctx->worker->progressed(ctx->id, ctx->doneBefore + qint64(current), ctx->total,
                                   ctx->currentName);
}

// Finish the payload before touching its final name. A failed/cancelled copy
// must not publish a truncated file or destroy an existing destination.
bool copyStaged(GFile *source, GFile *destination, bool replace, GCancellable *cancel,
                GFileProgressCallback progress, gpointer progressData, GError **error)
{
    GFile *parent = g_file_get_parent(destination);
    if (!parent) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Destination has no parent");
        return false;
    }
    const QByteArray name = (QStringLiteral(".omanta-copy-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8();
    GFile *staging = g_file_get_child(parent, name.constData());
    g_object_unref(parent);
    if (!g_file_make_directory(staging, cancel, error)) {
        g_object_unref(staging);
        return false; // never clean a path we did not create
    }
    GFile *payload = g_file_get_child(staging, "payload");
    bool ok = setDirectoryMode(staging, 0700, cancel, error);
    if (ok) {
        const auto flags = GFileCopyFlags(G_FILE_COPY_ALL_METADATA | G_FILE_COPY_NOFOLLOW_SYMLINKS);
        ok = g_file_copy(source, payload, flags, cancel, progress, progressData, error);
    }
    if (ok) {
        const auto flags = GFileCopyFlags(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_NO_FALLBACK_FOR_MOVE
            | (replace ? G_FILE_COPY_OVERWRITE : 0));
        ok = g_file_move(payload, destination, flags, cancel, nullptr, nullptr, error);
    }
    // Cancellation must not cancel cleanup. These are individual paths in
    // our exclusive directory, never a recursive delete of the destination.
    if (!ok)
        g_file_delete(payload, nullptr, nullptr);
    g_file_delete(staging, nullptr, nullptr);
    g_object_unref(payload);
    g_object_unref(staging);
    return ok;
}

} // namespace

FileOperationWorker::FileOperationWorker(QObject *parent)
    : QObject(parent), m_cancellable(g_cancellable_new())
{
}

FileOperationWorker::~FileOperationWorker()
{
    if (m_cancellable)
        g_object_unref(m_cancellable);
}

void FileOperationWorker::requestCancel()
{
    if (m_cancellable)
        g_cancellable_cancel(m_cancellable);
}

void FileOperationWorker::prepare()
{
    g_cancellable_reset(m_cancellable);
}

void FileOperationWorker::run(const FileOperationRequest &request, quint64 id)
{
    m_needsPassphrase = false;
    m_passphraseArchive.clear();

    FileOperationResult result;
    QString error;
    bool ok = false;

    if (g_cancellable_is_cancelled(m_cancellable)) {
        Q_EMIT failed(id, QStringLiteral("Cancelled"), result);
        return;
    }

    switch (request.kind) {
    case FileOperationRequest::CreateFolder:
        ok = doCreateFolder(request, result, &error);
        break;
    case FileOperationRequest::Rename:
        ok = doRename(request, result, &error);
        break;
    case FileOperationRequest::BatchRename:
        ok = doBatchRename(request, result, &error, id);
        break;
    case FileOperationRequest::CreateLink:
        ok = doCreateLink(request, result, &error);
        break;
    case FileOperationRequest::Compress:
        ok = doCompress(request, result, &error, id);
        break;
    case FileOperationRequest::Extract:
        ok = doExtract(request, result, &error, id);
        break;
    case FileOperationRequest::Trash:
        ok = doTrash(request, result, &error);
        break;
    case FileOperationRequest::RestoreFromTrash:
        ok = doRestore(request, result, &error);
        break;
    case FileOperationRequest::DeletePermanently:
        ok = doDelete(request, result, &error);
        break;
    case FileOperationRequest::EmptyTrash:
        ok = doEmptyTrash(request, result, &error);
        break;
    case FileOperationRequest::RemoveCreatedFolder:
        ok = doRemoveCreatedFolder(request, result, &error);
        break;
    case FileOperationRequest::UndoTransfer:
        ok = doUndoTransfer(request, &error, id);
        break;
    case FileOperationRequest::UndoExtraction:
        ok = ArchiveEngine::undoExtraction(request.created, &error,
            [this] { return bool(g_cancellable_is_cancelled(m_cancellable)); });
        break;
    case FileOperationRequest::Copy:
        ok = doTransfer(request, false, result, &error, id);
        break;
    case FileOperationRequest::Move:
        ok = doTransfer(request, true, result, &error, id);
        break;
    }

    if (ok)
        Q_EMIT succeeded(id, result);
    else if (m_needsPassphrase)
        Q_EMIT passphraseNeeded(id, m_passphraseArchive, result);
    else
        Q_EMIT failed(id, error.isEmpty() ? QStringLiteral("Operation failed") : error, result);
}

bool FileOperationWorker::doCreateFolder(const FileOperationRequest &request,
                                         FileOperationResult &result, QString *error)
{
    if (request.sources.isEmpty() || request.sources.first().isEmpty()) {
        *error = QStringLiteral("No folder name given");
        return false;
    }

    GFile *parent = Location::make(request.destination);
    GFile *target = g_file_get_child(parent, request.sources.first().toUtf8().constData());

    GError *gerror = nullptr;
    const bool ok = g_file_make_directory(target, m_cancellable, &gerror);
    if (ok)
        result.produced << pathOf(target);
    else
        *error = messageOf(gerror, "Could not create folder");

    g_clear_error(&gerror);
    g_object_unref(target);
    g_object_unref(parent);
    return ok;
}

bool FileOperationWorker::doCreateLink(const FileOperationRequest &request,
                                       FileOperationResult &result, QString *error)
{
    if (request.sources.isEmpty() || request.destination.isEmpty()) {
        *error = QStringLiteral("Nothing to link");
        return false;
    }

    GFile *directory = Location::make(request.destination);
    bool ok = true;

    for (const QString &path : request.sources) {
        const QString name = QStringLiteral("Link to %1").arg(QFileInfo(path).fileName());
        GFile *target = g_file_get_child(directory, name.toUtf8().constData());
        if (exists(target)) {
            g_object_unref(target);
            target = uniqueChild(directory, name);
        }
        if (!target) {
            *error = QStringLiteral("Could not find a free name for the link");
            ok = false;
            break;
        }

        GError *gerror = nullptr;
        // The symlink value is the absolute source path, as Nautilus writes
        // it — a link made in one folder and moved elsewhere keeps working.
        if (g_file_make_symbolic_link(target, path.toUtf8().constData(),
                                      m_cancellable, &gerror)) {
            result.produced << pathOf(target);
        } else {
            *error = messageOf(gerror, "Could not create link");
            ok = false;
        }
        g_clear_error(&gerror);
        g_object_unref(target);
        if (!ok)
            break;
    }

    g_object_unref(directory);
    return ok;
}

bool FileOperationWorker::doRename(const FileOperationRequest &request,
                                   FileOperationResult &result, QString *error)
{
    if (request.sources.isEmpty() || request.destination.isEmpty()) {
        *error = QStringLiteral("Nothing to rename");
        return false;
    }

    GFile *source = Location::make(request.sources.first());

    GError *gerror = nullptr;
    // set_display_name rather than move: it is the operation that means
    // "rename", and it refuses to silently clobber a different file.
    GFile *renamed = g_file_set_display_name(source, request.destination.toUtf8().constData(),
                                             m_cancellable, &gerror);
    if (renamed) {
        result.sources << request.sources.first();
        result.produced << pathOf(renamed);
        g_object_unref(renamed);
    } else {
        *error = messageOf(gerror, "Could not rename");
    }

    g_clear_error(&gerror);
    g_object_unref(source);
    return renamed != nullptr;
}

bool FileOperationWorker::doBatchRename(const FileOperationRequest &request,
                                        FileOperationResult &result, QString *error, quint64 id)
{
    const int count = request.sources.size();
    if (count == 0 || request.names.size() != count) {
        *error = QStringLiteral("Nothing to rename");
        return false;
    }

    QStringList oldNames;
    for (const QString &path : request.sources)
        oldNames << QFileInfo(path).fileName();

    // If any target name is currently held by a different member of the batch,
    // renaming in order would clash mid-flight ("2.jpg"→"3.jpg" while
    // "1.jpg"→"2.jpg"). Going through temporary names first makes order
    // irrelevant.
    QSet<QString> oldSet(oldNames.cbegin(), oldNames.cend());
    bool twoPhase = false;
    for (int i = 0; i < count && !twoPhase; ++i)
        twoPhase = request.names.at(i) != oldNames.at(i) && oldSet.contains(request.names.at(i));

    // Every completed rename, so a failure can put every name back. Renames
    // are cheap and invertible, which is what makes all-or-nothing honest
    // here in a way it could never be for a half-finished copy.
    struct Done {
        QString currentPath;
        QString previousName;
    };
    QList<Done> completed;

    const auto rollback = [&completed]() -> bool {
        bool restored = true;
        for (auto it = completed.crbegin(); it != completed.crend(); ++it) {
            GFile *file = Location::make(it->currentPath);
            // No cancellable: a rollback must run to the end once started.
            GFile *back = g_file_set_display_name(file, it->previousName.toUtf8().constData(),
                                                  nullptr, nullptr);
            if (back)
                g_object_unref(back);
            else
                restored = false;
            g_object_unref(file);
        }
        return restored;
    };

    QStringList current = request.sources;
    const qint64 totalSteps = qint64(count) * (twoPhase ? 2 : 1);
    qint64 done = 0;

    const auto renameTo = [&](int i, const QString &newName) -> bool {
        if (g_cancellable_is_cancelled(m_cancellable)) {
            *error = QStringLiteral("Cancelled");
            return false;
        }
        GFile *file = Location::make(current.at(i));
        GError *gerror = nullptr;
        GFile *renamed = g_file_set_display_name(file, newName.toUtf8().constData(),
                                                 m_cancellable, &gerror);
        if (renamed) {
            completed.append({ pathOf(renamed), QFileInfo(current.at(i)).fileName() });
            current[i] = pathOf(renamed);
            g_object_unref(renamed);
        } else {
            *error = messageOf(gerror, "Could not rename");
        }
        g_clear_error(&gerror);
        g_object_unref(file);
        if (renamed)
            Q_EMIT progressed(id, ++done, totalSteps, newName);
        return renamed != nullptr;
    };

    for (int phase = twoPhase ? 0 : 1; phase < 2; ++phase) {
        for (int i = 0; i < count; ++i) {
            if (request.names.at(i) == oldNames.at(i))
                continue; // already right; renaming would be a pointless failure risk
            const QString target = phase == 0
                ? QStringLiteral(".omanta-batch-%1-%2").arg(id).arg(i)
                : request.names.at(i);
            if (!renameTo(i, target)) {
                if (!rollback())
                    *error += QStringLiteral(" — and some earlier renames could not be undone");
                return false;
            }
        }
    }

    for (int i = 0; i < count; ++i) {
        if (request.names.at(i) == oldNames.at(i))
            continue;
        result.sources << request.sources.at(i);
        result.produced << current.at(i);
    }
    return true;
}

bool FileOperationWorker::doCompress(const FileOperationRequest &request,
                                     FileOperationResult &result, QString *error, quint64 id)
{
    QElapsedTimer throttle;
    throttle.start();
    const bool ok = ArchiveEngine::compress(
        request.sources, request.destination, error,
        [this] { return bool(g_cancellable_is_cancelled(m_cancellable)); },
        [&](qint64 done, qint64 total) {
            if (throttle.elapsed() < 100)
                return;
            throttle.restart();
            Q_EMIT progressed(id, done, total, QFileInfo(request.destination).fileName());
        },
        request.password);
    if (ok) {
        result.sources = request.sources;
        result.produced << request.destination;
    }
    return ok;
}

bool FileOperationWorker::doExtract(const FileOperationRequest &request,
                                    FileOperationResult &result, QString *error, quint64 id)
{
    QElapsedTimer throttle;
    throttle.start();
    for (const QString &archive : request.sources) {
        QString produced;
        bool needsPassphrase = false;
        const bool ok = ArchiveEngine::extract(
            archive, request.destination, &produced, error,
            [this] { return bool(g_cancellable_is_cancelled(m_cancellable)); },
            [&](qint64 done, qint64 total) {
                if (throttle.elapsed() < 100)
                    return;
                throttle.restart();
                Q_EMIT progressed(id, done, total, QFileInfo(archive).fileName());
            },
            request.password, &needsPassphrase, &result.created);
        // Fail fast, but report what already landed — like a partial trash,
        // the completed extractions are real and stay.
        if (!ok) {
            if (needsPassphrase) {
                m_needsPassphrase = true;
                m_passphraseArchive = QFileInfo(archive).fileName();
            }
            return false;
        }
        result.sources << archive;
        result.produced << produced;
    }
    return true;
}

bool FileOperationWorker::doTrash(const FileOperationRequest &request,
                                  FileOperationResult &result, QString *error)
{
    for (const QString &path : request.sources) {
        GFile *file = Location::make(path);
        GError *gerror = nullptr;
        const bool ok = g_file_trash(file, m_cancellable, &gerror);
        g_object_unref(file);

        if (!ok) {
            *error = messageOf(gerror, "Could not move to trash");
            if (g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
                result.trashUnavailable << path;
                g_clear_error(&gerror);
                continue; // determine which other sources actually lack Trash
            }
            g_clear_error(&gerror);
            // Report what did make it, so undo can still put those back.
            return false;
        }
        g_clear_error(&gerror);
        result.sources << path;
    }
    return result.trashUnavailable.isEmpty();
}

bool FileOperationWorker::doRestore(const FileOperationRequest &request,
                                    FileOperationResult &result, QString *error)
{
    // GIO does not tell you where a trashed file went, so undoing a trash means
    // searching trash:/// for the entry whose trash::orig-path is the file we
    // deleted. This is exactly what Nautilus does.
    GFile *trash = g_file_new_for_uri("trash:///");
    GError *gerror = nullptr;
    GFileEnumerator *entries = g_file_enumerate_children(
        trash,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
        G_FILE_QUERY_INFO_NONE, m_cancellable, &gerror);

    if (!entries) {
        *error = messageOf(gerror, "Could not read the trash");
        g_clear_error(&gerror);
        g_object_unref(trash);
        return false;
    }

    QStringList wanted = request.sources;
    bool ok = true;

    while (GFileInfo *info = g_file_enumerator_next_file(entries, m_cancellable, &gerror)) {
        const char *orig = g_file_info_get_attribute_byte_string(info,
                                                                 G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
        const QString original = QString::fromUtf8(orig ? orig : "");

        if (!original.isEmpty() && wanted.contains(original)) {
            GFile *inTrash = g_file_get_child(trash, g_file_info_get_name(info));
            GFile *target = Location::make(original);

            GError *moveError = nullptr;
            // G_FILE_COPY_NONE: if something now occupies the original path,
            // restoring must fail loudly rather than overwrite it.
            if (g_file_move(inTrash, target, G_FILE_COPY_NONE, m_cancellable,
                            nullptr, nullptr, &moveError)) {
                result.produced << original;
                wanted.removeAll(original);
            } else {
                *error = messageOf(moveError, "Could not restore from trash");
                ok = false;
            }
            g_clear_error(&moveError);
            g_object_unref(target);
            g_object_unref(inTrash);
        }
        g_object_unref(info);

        if (!ok || wanted.isEmpty())
            break;
    }

    if (gerror) {
        *error = messageOf(gerror, "Could not read the trash");
        g_clear_error(&gerror);
        ok = false;
    }
    g_object_unref(entries);
    // With no Trash monitor, GVfs retains its directory snapshot after a
    // restore. Refresh it before another trash operation can reuse the same
    // basename; otherwise that new entry can be absent from trash:///.
    if (!result.produced.isEmpty()) {
        GFileInfo *refreshed = g_file_query_info(trash, G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT,
            G_FILE_QUERY_INFO_NONE, m_cancellable, nullptr);
        g_clear_object(&refreshed);
    }
    g_object_unref(trash);

    if (ok && !wanted.isEmpty()) {
        *error = QStringLiteral("Could not find %1 in the trash").arg(wanted.first());
        return false;
    }
    return ok;
}

bool FileOperationWorker::doEmptyTrash(const FileOperationRequest &request,
                                       FileOperationResult &result, QString *error)
{
    Q_UNUSED(request)
    Q_UNUSED(result)

    GFile *trash = g_file_new_for_uri("trash:///");
    GError *gerror = nullptr;
    GFileEnumerator *entries = g_file_enumerate_children(
        trash, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        m_cancellable, &gerror);

    if (!entries) {
        *error = messageOf(gerror, "Could not read the trash");
        g_clear_error(&gerror);
        g_object_unref(trash);
        return false;
    }

    // Each top-level entry is a single delete: the gvfs trash backend removes
    // a trashed directory and its contents in one call, and rejects deletes
    // any deeper ("Items in the trash may not be modified").
    bool ok = true;
    while (GFileInfo *info = g_file_enumerator_next_file(entries, m_cancellable, nullptr)) {
        GFile *child = g_file_get_child(trash, g_file_info_get_name(info));
        if (!deleteRecursively(child, error))
            ok = false;
        g_object_unref(child);
        g_object_unref(info);
        if (!ok || g_cancellable_is_cancelled(m_cancellable))
            break;
    }

    g_object_unref(entries);
    g_object_unref(trash);
    return ok;
}

bool FileOperationWorker::deleteRecursively(GFile *file, QString *error)
{
    if (g_cancellable_is_cancelled(m_cancellable)) {
        *error = QStringLiteral("Cancelled");
        return false;
    }

    // The gvfs trash backend deletes a top-level item and everything under it
    // in one call, and refuses any delete deeper inside trash:// ("Items in
    // the trash may not be modified") — so trash items must not be recursed.
    if (!g_file_has_uri_scheme(file, "trash") && isDirectory(file)) {
        GError *gerror = nullptr;
        GFileEnumerator *children = g_file_enumerate_children(
            file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            m_cancellable, &gerror);

        if (!children) {
            *error = messageOf(gerror, "Could not read folder");
            g_clear_error(&gerror);
            return false;
        }

        while (GFileInfo *info = g_file_enumerator_next_file(children, m_cancellable, nullptr)) {
            GFile *child = g_file_get_child(file, g_file_info_get_name(info));
            const bool ok = deleteRecursively(child, error);
            g_object_unref(child);
            g_object_unref(info);
            if (!ok) {
                g_object_unref(children);
                return false;
            }
        }
        g_object_unref(children);
    }

    GError *gerror = nullptr;
    const bool ok = g_file_delete(file, m_cancellable, &gerror);
    if (!ok)
        *error = messageOf(gerror, "Could not delete");
    g_clear_error(&gerror);
    return ok;
}

bool FileOperationWorker::doDelete(const FileOperationRequest &request,
                                   FileOperationResult &result, QString *error)
{
    for (const QString &path : request.sources) {
        GFile *file = Location::make(path);
        const bool ok = deleteRecursively(file, error);
        g_object_unref(file);
        if (!ok)
            return false;
        result.sources << path;
    }
    return true;
}

bool FileOperationWorker::doRemoveCreatedFolder(const FileOperationRequest &request,
                                                FileOperationResult &result, QString *error)
{
    for (const QString &path : request.sources) {
        GFile *file = Location::make(path);
        GError *gerror = nullptr;

        // Non-recursive on purpose. If the user has put something in the folder
        // since it was created, undoing the creation must not take it with it.
        const bool ok = g_file_delete(file, m_cancellable, &gerror);
        const bool notEmpty = g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY);
        g_object_unref(file);

        if (!ok) {
            *error = notEmpty
                ? QStringLiteral("“%1” is no longer empty, so it was left alone")
                      .arg(QFileInfo(path).fileName())
                : messageOf(gerror, "Could not remove folder");
            g_clear_error(&gerror);
            return false;
        }
        g_clear_error(&gerror);
        result.sources << path;
    }
    return true;
}

bool FileOperationWorker::buildPlan(GFile *source, GFile *destination, ConflictPolicy policy,
                                    QList<PlanItem> &plan, qint64 *totalBytes,
                                    QStringList *skipped, QString *error)
{
    if (g_cancellable_is_cancelled(m_cancellable)) {
        *error = QStringLiteral("Cancelled");
        return false;
    }

    const bool sourceIsDir = isDirectory(source);

    // `target` is this function's own reference from here on, and every exit
    // path either hands it to the plan or releases it. The caller's reference
    // is never consumed.
    GFile *target = g_object_ref(destination);

    if (exists(target)) {
        if (policy == ConflictPolicy::Fail) {
            *error = QStringLiteral("“%1” already exists").arg(pathOf(target));
            g_object_unref(target);
            return false;
        }
        // Two directories with the same name merge, which is what every file
        // manager does and what users expect when dropping a folder onto one.
        const bool targetIsDir = isDirectory(target);
        if (policy == ConflictPolicy::Replace && sourceIsDir != targetIsDir) {
            // GIO cannot publish a directory over a file atomically across
            // all backends. Never delete the existing item to make room.
            *error = QStringLiteral("Cannot replace “%1”: files and folders cannot replace each other. "
                                    "Rename one of the items first.").arg(pathOf(target));
            g_object_unref(target);
            return false;
        }
        const bool bothDirs = sourceIsDir && targetIsDir;
        if (!bothDirs) {
            switch (policy) {
            case ConflictPolicy::Skip:
                *skipped << pathOf(source);
                g_object_unref(target);
                return true;
            case ConflictPolicy::Fail: // handled above
            case ConflictPolicy::Replace:
                break; // handled by the overwrite flag at copy time
            case ConflictPolicy::RenameNew: {
                GFile *parent = g_file_get_parent(target);
                char *base = g_file_get_basename(target);
                GFile *unique = parent ? uniqueChild(parent, QString::fromUtf8(base)) : nullptr;
                g_free(base);
                if (parent)
                    g_object_unref(parent);
                if (!unique) {
                    *error = QStringLiteral("Could not find a free name");
                    g_object_unref(target);
                    return false;
                }
                g_object_unref(target);
                target = unique;
                break;
            }
            }
        }
    }

    if (!sourceIsDir) {
        const qint64 size = sizeOf(source);
        plan.append(PlanItem{ g_object_ref(source), target, false, size });
        *totalBytes += size;
        return true;
    }

    plan.append(PlanItem{ g_object_ref(source), target, true, 0 });
    destination = target; // children resolve against the possibly-renamed folder

    GError *gerror = nullptr;
    GFileEnumerator *children = g_file_enumerate_children(
        source, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        m_cancellable, &gerror);

    if (!children) {
        *error = messageOf(gerror, "Could not read folder");
        g_clear_error(&gerror);
        return false;
    }

    bool ok = true;
    while (GFileInfo *info = g_file_enumerator_next_file(children, m_cancellable, &gerror)) {
        GFile *childSource = g_file_get_child(source, g_file_info_get_name(info));
        GFile *childDestination = g_file_get_child(destination, g_file_info_get_name(info));

        ok = buildPlan(childSource, childDestination, policy, plan, totalBytes, skipped, error);

        g_object_unref(childDestination);
        g_object_unref(childSource);
        g_object_unref(info);

        if (!ok)
            break;
    }

    if (gerror) {
        *error = messageOf(gerror, "Could not read folder");
        g_clear_error(&gerror);
        ok = false;
    }
    g_object_unref(children);
    return ok;
}

bool FileOperationWorker::doTransfer(const FileOperationRequest &request, bool removeSources,
                                     FileOperationResult &result, QString *error, quint64 id,
                                     const QStringList &targets)
{
    GFile *destinationDir = Location::make(request.destination);
    QList<PlanItem> plan;
    qint64 totalBytes = 0;
    auto cleanup = [&] {
        for (const PlanItem &item : plan) {
            g_object_unref(item.source);
            g_object_unref(item.destination);
        }
        g_object_unref(destinationDir);
    };
    auto fail = [&] { cleanup(); return false; };

    for (int i = 0; i < request.sources.size(); ++i) {
        const QString &path = request.sources.at(i);
        GFile *source = Location::make(path);
        const QString name = Location::displayName(path);
        GFile *destination = targets.isEmpty()
            ? g_file_get_child(destinationDir, name.toUtf8().constData())
            : Location::make(targets.at(i));
        if (g_file_equal(source, destination)) {
            if (request.policy == ConflictPolicy::RenameNew) {
                g_object_unref(destination);
                destination = uniqueChild(destinationDir, name);
            } else {
                result.skipped << path;
                g_object_unref(destination);
                g_object_unref(source);
                continue;
            }
        }
        if (!destination || g_file_has_prefix(destination, source)) {
            *error = destination ? QStringLiteral("Cannot put “%1” inside itself").arg(name)
                                 : QStringLiteral("Could not find a free name for “%1”").arg(name);
            g_clear_object(&destination);
            g_object_unref(source);
            return fail();
        }

        const bool existed = exists(destination);
        // Handle conflicts in the recursive plan, including directory merges.
        // NO_FALLBACK_FOR_MOVE makes cross-device moves use our own journalled
        // copy/delete path instead of GIO's file-only fallback.
        if (removeSources && !existed) {
            GError *gerror = nullptr;
            const auto flags = GFileCopyFlags(G_FILE_COPY_NOFOLLOW_SYMLINKS
                                               | G_FILE_COPY_NO_FALLBACK_FOR_MOVE);
            if (g_file_move(source, destination, flags, m_cancellable, nullptr, nullptr, &gerror)) {
                result.sources << path;
                result.produced << pathOf(destination);
                result.transfers.append({path, pathOf(destination), false, true, true});
                g_object_unref(destination);
                g_object_unref(source);
                continue;
            }
            const bool fallback = g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)
                || g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)
                || g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_WOULD_MERGE)
                || g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_EXISTS);
            if (!fallback)
                *error = messageOf(gerror, "Could not move");
            g_clear_error(&gerror);
            if (!fallback) {
                g_object_unref(destination);
                g_object_unref(source);
                return fail();
            }
        }

        const int before = plan.size();
        const bool ok = buildPlan(source, destination, request.policy, plan, &totalBytes,
                                  &result.skipped, error);
        g_object_unref(destination);
        g_object_unref(source);
        if (!ok)
            return fail();
        if (plan.size() > before) {
            result.sources << path;
            result.produced << pathOf(plan.at(before).destination);
        }
    }

    const int firstEntry = result.transfers.size();
    QElapsedTimer throttle;
    throttle.start();
    qint64 done = 0;
    for (const PlanItem &item : plan) {
        if (g_cancellable_is_cancelled(m_cancellable)) {
            *error = QStringLiteral("Cancelled");
            return fail();
        }
        GError *gerror = nullptr;
        const bool existed = exists(item.destination);
        TransferEntry entry{pathOf(item.source), pathOf(item.destination), item.isDirectory,
                            !existed, false};
        if (item.isDirectory) {
            GFileInfo *info = g_file_query_info(item.source, G_FILE_ATTRIBUTE_UNIX_MODE,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, m_cancellable, nullptr);
            if (info) {
                entry.mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
                g_object_unref(info);
            }
            if (existed && !isDirectory(item.destination)) {
                // Recheck after planning: another process or an earlier
                // source in this batch may have occupied the folder's name.
                *error = QStringLiteral("Cannot replace “%1”: a non-folder item occupies the folder's name. "
                                        "Rename one of the items first.").arg(entry.destination);
                return fail();
            }
            if (!existed) {
                if (!g_file_make_directory(item.destination, m_cancellable, &gerror)) {
                    *error = messageOf(gerror, "Could not create folder");
                    g_clear_error(&gerror);
                    return fail();
                }
                entry.created = true;
                // Restrict the empty container before writing any contents.
                // Final source metadata is applied after its children exist.
                if (entry.mode && !setDirectoryMode(item.destination, 0700, m_cancellable, &gerror)) {
                    *error = messageOf(gerror, "Could not set folder permissions");
                    g_clear_error(&gerror);
                    return fail();
                }
            }
            result.transfers.append(entry);
            continue;
        }
        const QString currentName = Location::displayName(entry.source);
        ProgressContext ctx{this, id, done, totalBytes, currentName, &throttle};
        if (!copyStaged(item.source, item.destination, request.policy == ConflictPolicy::Replace,
                        m_cancellable, onCopyProgress, &ctx, &gerror)) {
            if (g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_EXISTS)
                && request.policy == ConflictPolicy::Skip) {
                result.skipped << entry.source;
                g_clear_error(&gerror);
                continue;
            }
            *error = messageOf(gerror, "Could not copy");
            g_clear_error(&gerror);
            return fail();
        }
        if (existed)
            result.undoable = false;
        result.transfers.append(entry);
        done += item.size;
        Q_EMIT progressed(id, done, totalBytes, currentName);
    }

    // Only new directories receive metadata. Merged containers belong to
    // the destination and must retain their original permissions and owner.
    for (int i = result.transfers.size() - 1; i >= firstEntry; --i) {
        const auto *it = &result.transfers.at(i);
        if (!it->directory || !it->created)
            continue;
        GFile *source = Location::make(it->source);
        GFile *destination = Location::make(it->destination);
        GError *gerror = nullptr;
        const bool ok = copyDirectoryMetadata(source, destination, m_cancellable, &gerror);
        g_object_unref(source);
        g_object_unref(destination);
        if (!ok) {
            *error = messageOf(gerror, "Could not preserve folder metadata");
            g_clear_error(&gerror);
            return fail();
        }
    }

    // Delete only entries actually copied, children first. In particular a
    // skipped child remains at the source, along with its containing folders.
    if (removeSources) {
        for (int i = result.transfers.size() - 1; i >= firstEntry; --i) {
            TransferEntry &entry = result.transfers[i];
            GFile *source = Location::make(entry.source);
            GError *gerror = nullptr;
            entry.moved = g_file_delete(source, m_cancellable, &gerror);
            g_object_unref(source);
            const bool keptChildren = entry.directory
                && g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY)
                && !result.skipped.isEmpty();
            if (!entry.moved && !keptChildren) {
                *error = messageOf(gerror, "Could not remove the original");
                g_clear_error(&gerror);
                return fail();
            }
            g_clear_error(&gerror);
        }
    }
    cleanup();
    return true;
}

bool FileOperationWorker::doUndoTransfer(const FileOperationRequest &request,
                                         QString *error, quint64 id)
{
    // Refuse conflicts before changing anything. Restoring a move must not
    // silently choose a different name or overwrite a replacement original.
    for (const TransferEntry &entry : request.transfers) {
        if (!entry.moved)
            continue;
        GFile *source = Location::make(entry.source);
        const bool occupied = exists(source);
        g_object_unref(source);
        if (occupied) {
            *error = QStringLiteral("“%1” already exists").arg(entry.source);
            return false;
        }
    }
    for (const TransferEntry &entry : request.transfers) {
        if (!entry.moved)
            continue;
        if (entry.directory) {
            GFile *source = Location::make(entry.source);
            GError *gerror = nullptr;
            bool ok = g_file_make_directory(source, m_cancellable, &gerror);
            if (ok && entry.mode)
                ok = setDirectoryMode(source, 0700, m_cancellable, &gerror);
            g_object_unref(source);
            if (!ok) {
                *error = messageOf(gerror, "Could not restore folder");
                g_clear_error(&gerror);
                return false;
            }
        } else {
            FileOperationRequest move;
            move.kind = FileOperationRequest::Move;
            move.sources = {entry.destination};
            move.destination = Location::parent(entry.source);
            move.policy = ConflictPolicy::Fail;
            FileOperationResult ignored;
            if (!doTransfer(move, true, ignored, error, id, {entry.source}))
                return false;
        }
    }
    for (auto it = request.transfers.crbegin(); it != request.transfers.crend(); ++it) {
        GError *gerror = nullptr;
        if (it->directory && it->moved && it->mode) {
            GFile *source = Location::make(it->source);
            const bool ok = setDirectoryMode(source, it->mode, m_cancellable, &gerror);
            g_object_unref(source);
            if (!ok) {
                *error = messageOf(gerror, "Could not restore folder permissions");
                g_clear_error(&gerror);
                return false;
            }
        }
        if (!it->created || (it->moved && !it->directory))
            continue;
        GFile *destination = Location::make(it->destination);
        // Nonrecursive: never remove files added to a copied folder later.
        const bool ok = g_file_delete(destination, m_cancellable, &gerror);
        g_object_unref(destination);
        if (!ok) {
            *error = messageOf(gerror, "Could not remove the copy");
            g_clear_error(&gerror);
            return false;
        }
    }
    return true;
}
