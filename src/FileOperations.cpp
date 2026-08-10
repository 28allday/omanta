#include "FileOperations.h"

#include "FileOperationWorker.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

QString FileOperationRequest::describe() const
{
    const int count = sources.size();
    const QString what = count == 1 ? QFileInfo(sources.first()).fileName()
                                    : QStringLiteral("%1 items").arg(count);
    switch (kind) {
    case CreateFolder: return QStringLiteral("Create “%1”").arg(sources.value(0));
    case Rename: return QStringLiteral("Rename to “%1”").arg(destination);
    case BatchRename: return QStringLiteral("Rename %1").arg(what);
    case Compress: return QStringLiteral("Compress %1").arg(what);
    case Extract: return QStringLiteral("Extract %1").arg(what);
    case CreateLink: return QStringLiteral("Link %1").arg(what);
    case Trash: return QStringLiteral("Move %1 to trash").arg(what);
    case RestoreFromTrash: return QStringLiteral("Restore %1").arg(what);
    case Copy: return QStringLiteral("Copy %1").arg(what);
    case Move: return QStringLiteral("Move %1").arg(what);
    case DeletePermanently: return QStringLiteral("Delete %1").arg(what);
    case EmptyTrash: return QStringLiteral("Empty the trash");
    case RemoveCreatedFolder: return QStringLiteral("Remove %1").arg(what);
    }
    return QStringLiteral("Operation");
}

QString FileOperationRequest::shortStatus() const
{
    const int count = sources.size();
    const QString what = count == 1
        ? QStringLiteral("“%1”").arg(QFileInfo(sources.first()).fileName())
        : QStringLiteral("%1 files").arg(count);
    switch (kind) {
    case CreateFolder: return QStringLiteral("Creating “%1”").arg(sources.value(0));
    case Rename: return QStringLiteral("Renaming to “%1”").arg(destination);
    case BatchRename: return QStringLiteral("Renaming %1").arg(what);
    case Compress: return QStringLiteral("Compressing %1").arg(what);
    case Extract: return QStringLiteral("Extracting %1").arg(what);
    case CreateLink: return QStringLiteral("Linking %1").arg(what);
    case Trash: return QStringLiteral("Moving %1 to trash").arg(what);
    case RestoreFromTrash: return QStringLiteral("Restoring %1").arg(what);
    case Copy: return QStringLiteral("Copying %1").arg(what);
    case Move: return QStringLiteral("Moving %1").arg(what);
    case DeletePermanently: return QStringLiteral("Deleting %1").arg(what);
    case EmptyTrash: return QStringLiteral("Emptying the trash");
    case RemoveCreatedFolder: return QStringLiteral("Removing %1").arg(what);
    }
    return QStringLiteral("Working");
}

FileOperations::FileOperations(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FileOperationRequest>();
    qRegisterMetaType<FileOperationResult>();

    m_worker = new FileOperationWorker;
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &FileOperations::dispatch, m_worker, &FileOperationWorker::run);
    connect(m_worker, &FileOperationWorker::progressed, this, &FileOperations::handleProgress);
    connect(m_worker, &FileOperationWorker::succeeded, this, &FileOperations::handleSuccess);
    connect(m_worker, &FileOperationWorker::failed, this, &FileOperations::handleFailure);
    connect(m_worker, &FileOperationWorker::passphraseNeeded,
            this, &FileOperations::handlePassphraseNeeded);

    m_thread.start();
}

FileOperations::~FileOperations()
{
    if (m_worker)
        m_worker->requestCancel();
    m_thread.quit();
    m_thread.wait(5000);
}

QString FileOperations::undoLabel() const
{
    return m_undoStack.isEmpty() ? QString() : m_undoStack.last().label;
}

QString FileOperations::redoLabel() const
{
    return m_redoStack.isEmpty() ? QString() : m_redoStack.last().label;
}

void FileOperations::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void FileOperations::setStatus(const QString &text, qreal progress)
{
    m_statusText = text;
    m_progress = progress;
    Q_EMIT progressChanged();
}

void FileOperations::setError(const QString &message)
{
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

void FileOperations::clearError()
{
    setError(QString());
}

void FileOperations::enqueue(const FileOperationRequest &request)
{
    // A fresh operation forks history: whatever was undone can no longer be
    // replayed on top of a world that has moved on.
    if (!m_redoStack.isEmpty()) {
        m_redoStack.clear();
        Q_EMIT historyChanged();
    }
    Pending pending;
    pending.request = request;
    enqueuePending(pending);
}

void FileOperations::enqueuePending(Pending pending)
{
    pending.id = m_nextId++;
    m_queue.enqueue(pending);
    if (!m_busy)
        startNext();
    else
        Q_EMIT operationsChanged(); // startNext announces its own pick
}

QVariantList FileOperations::operations() const
{
    QVariantList out;
    if (m_busy) {
        QVariantMap running;
        running.insert(QStringLiteral("id"), double(m_current.id));
        running.insert(QStringLiteral("label"), m_current.request.describe());
        running.insert(QStringLiteral("shortStatus"), m_current.request.shortStatus());
        running.insert(QStringLiteral("state"), QStringLiteral("running"));
        running.insert(QStringLiteral("progress"), m_progress);
        running.insert(QStringLiteral("detail"), m_currentDetail);
        const qint64 elapsed = m_currentClock.isValid() ? m_currentClock.elapsed() : 0;
        running.insert(QStringLiteral("transferred"),
                       transferredText(m_currentDone, m_currentTotal, elapsed));
        running.insert(QStringLiteral("remaining"),
                       remainingText(m_currentDone, m_currentTotal, elapsed));
        out.append(running);
    }
    for (const Pending &pending : m_queue) {
        QVariantMap queued;
        queued.insert(QStringLiteral("id"), double(pending.id));
        queued.insert(QStringLiteral("label"), pending.request.describe());
        queued.insert(QStringLiteral("shortStatus"), pending.request.shortStatus());
        queued.insert(QStringLiteral("state"), QStringLiteral("queued"));
        queued.insert(QStringLiteral("progress"), 0.0);
        queued.insert(QStringLiteral("detail"), QString());
        queued.insert(QStringLiteral("transferred"), QString());
        queued.insert(QStringLiteral("remaining"), QString());
        out.append(queued);
    }
    return out;
}

QString FileOperations::transferredText(qint64 done, qint64 total, qint64 elapsedMs)
{
    // Nautilus's details line: "1.2 GB / 10 GB (10.3 MB/s)". Bytes as soon as
    // they exist; the rate only once it means something (same guard as the
    // time estimate).
    if (done <= 0 || total <= 0)
        return {};
    // SI units (kB/MB), matching GLib's g_format_size — the sizes shown here
    // must agree with the ones in the Size column, which come from GLib.
    const QLocale locale;
    const auto fmt = QLocale::DataSizeSIFormat;
    QString text = QStringLiteral("%1 / %2")
                       .arg(locale.formattedDataSize(done, 1, fmt),
                            locale.formattedDataSize(total, 1, fmt));
    if (elapsedMs >= 1500 && done < total) {
        const qint64 perSecond = qint64(double(done) * 1000.0 / double(elapsedMs));
        if (perSecond > 0)
            text += QStringLiteral(" (%1/s)").arg(locale.formattedDataSize(perSecond, 1, fmt));
    }
    return text;
}

QString FileOperations::remainingText(qint64 done, qint64 total, qint64 elapsedMs)
{
    // No estimate is better than a wild one: wait for real bytes and enough
    // elapsed time for the rate to mean something.
    if (done <= 0 || total <= 0 || done >= total || elapsedMs < 1500)
        return {};
    const double rate = double(done) / double(elapsedMs); // bytes per ms
    const qint64 secondsLeft = qint64((double(total - done) / rate) / 1000.0) + 1;
    if (secondsLeft < 5)
        return QStringLiteral("A few seconds left");
    if (secondsLeft < 90)
        return QStringLiteral("About %1 seconds left").arg(secondsLeft);
    return QStringLiteral("About %1 minutes left").arg((secondsLeft + 59) / 60);
}

void FileOperations::cancelOperation(double id)
{
    const quint64 wanted = quint64(id);
    if (m_busy && m_current.id == wanted) {
        if (m_worker)
            m_worker->requestCancel();
        return; // the failure path announces the change
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).id == wanted) {
            m_queue.removeAt(i);
            Q_EMIT operationsChanged();
            return;
        }
    }
}

void FileOperations::startNext()
{
    if (m_queue.isEmpty()) {
        setBusy(false);
        setStatus(QString(), 0.0);
        Q_EMIT operationsChanged();
        return;
    }

    m_current = m_queue.dequeue();
    setBusy(true);
    setStatus(m_current.request.describe(), 0.0);
    m_currentDetail.clear();
    m_currentDone = 0;
    m_currentTotal = 0;
    m_currentClock.restart();
    Q_EMIT operationsChanged();

    // Queued across the thread boundary; the worker owns the blocking part.
    Q_EMIT dispatch(m_current.request, m_current.id);
}

void FileOperations::handleProgress(quint64 id, qint64 done, qint64 total, const QString &currentName)
{
    if (id != m_current.id)
        return;
    const qreal fraction = total > 0 ? qreal(done) / qreal(total) : 0.0;
    m_currentDone = done;
    m_currentTotal = total;
    m_currentDetail = currentName;
    setStatus(currentName.isEmpty() ? m_current.request.describe() : currentName, fraction);
    Q_EMIT operationsChanged();
}

void FileOperations::handleSuccess(quint64 id, const FileOperationResult &result)
{
    if (id != m_current.id)
        return;

    // An undo must not itself be undoable, or Ctrl+Z becomes a toggle that
    // ping-pongs instead of walking back through history. What it does
    // instead is arm the redo — only now, on success: a failed undo must
    // not offer to "redo" something still done.
    if (m_current.isUndo) {
        m_redoStack.append({ m_current.redoLabel, m_current.redoRequest });
        Q_EMIT historyChanged();
    } else {
        // A redo records its undo again like any fresh operation — undo,
        // redo, undo walks the same step both ways.
        recordUndo(m_current.request, result);
    }

    Q_EMIT operationFinished(m_current.request.describe());
    startNext();
}

void FileOperations::handleFailure(quint64 id, const QString &message)
{
    if (id != m_current.id)
        return;

    setError(message);

    // A failed operation records nothing: there is no reliable inverse for
    // "half of it happened".
    startNext();
}

void FileOperations::handlePassphraseNeeded(quint64 id, const QString &archiveName)
{
    if (id != m_current.id)
        return;

    // Not an error — the request parks (its stale password cleared) while
    // the window asks, and the queue moves on in the meantime.
    m_passphraseRequest = m_current.request;
    m_passphraseRequest.password.clear();
    m_awaitingPassphrase = true;
    Q_EMIT passphraseNeeded(archiveName);
    startNext();
}

void FileOperations::recordUndo(const FileOperationRequest &request,
                                const FileOperationResult &result)
{
    UndoEntry entry;
    entry.label = request.describe();
    // Redo is the original request replayed verbatim — deterministic given
    // the same disk state (RenameNew resolves conflicts the same way), and
    // incapable of overwriting anything if the state drifted meanwhile.
    entry.redo = request;

    switch (request.kind) {
    case FileOperationRequest::CreateFolder:
        if (result.produced.isEmpty())
            return;
        // Not trash: trash does not exist on every filesystem (tmpfs has none),
        // so an undo built on it fails exactly where the user least expects.
        // RemoveCreatedFolder deletes the folder only while it is still empty.
        entry.inverse.kind = FileOperationRequest::RemoveCreatedFolder;
        entry.inverse.sources = result.produced;
        break;

    case FileOperationRequest::Rename:
        if (result.produced.isEmpty() || result.sources.isEmpty())
            return;
        entry.inverse.kind = FileOperationRequest::Rename;
        entry.inverse.sources = result.produced;
        entry.inverse.destination = QFileInfo(result.sources.first()).fileName();
        break;

    case FileOperationRequest::BatchRename:
        if (result.produced.isEmpty() || result.produced.size() != result.sources.size())
            return;
        // The inverse is the same batch with old and new swapped — one Ctrl+Z
        // puts every name back, through the same two-phase machinery.
        entry.inverse.kind = FileOperationRequest::BatchRename;
        entry.inverse.sources = result.produced;
        for (const QString &source : result.sources)
            entry.inverse.names << QFileInfo(source).fileName();
        break;

    case FileOperationRequest::Trash:
        if (result.sources.isEmpty())
            return;
        entry.inverse.kind = FileOperationRequest::RestoreFromTrash;
        entry.inverse.sources = result.sources;
        break;

    case FileOperationRequest::Copy:
        if (result.produced.isEmpty())
            return;
        // Undoing a copy removes the copies, never the originals. Deleted
        // rather than trashed: these files were created seconds ago by this
        // app, trash is unavailable on some filesystems, and filling the bin
        // with undone copies is not what anyone means by "undo".
        entry.inverse.kind = FileOperationRequest::DeletePermanently;
        entry.inverse.sources = result.produced;
        break;

    case FileOperationRequest::Move: {
        if (result.produced.isEmpty() || result.sources.isEmpty())
            return;
        // Move them back to where they came from. Only valid when every source
        // shared a parent, which is true for a selection in one folder — and
        // if it isn't, recording no undo is better than recording a wrong one.
        const QString originalParent = QFileInfo(result.sources.first()).absolutePath();
        for (const QString &source : result.sources) {
            if (QFileInfo(source).absolutePath() != originalParent)
                return;
        }
        entry.inverse.kind = FileOperationRequest::Move;
        entry.inverse.sources = result.produced;
        entry.inverse.destination = originalParent;
        break;
    }

    case FileOperationRequest::RestoreFromTrash:
        // A deliberate restore (not the undo of a trash — undos never record)
        // is inverted by trashing the restored files again.
        if (result.produced.isEmpty())
            return;
        entry.inverse.kind = FileOperationRequest::Trash;
        entry.inverse.sources = result.produced;
        break;

    case FileOperationRequest::Compress:
    case FileOperationRequest::Extract:
    case FileOperationRequest::CreateLink:
        // Same shape as undoing a copy: what came out is deleted, what went
        // in is untouched. The archive keeps the originals (extract), the
        // originals keep themselves (compress), and a symlink's target never
        // belonged to the link — deleting the product loses nothing.
        if (result.produced.isEmpty())
            return;
        entry.inverse.kind = FileOperationRequest::DeletePermanently;
        entry.inverse.sources = result.produced;
        break;

    case FileOperationRequest::RemoveCreatedFolder:
    case FileOperationRequest::DeletePermanently:
    case FileOperationRequest::EmptyTrash:
        // A permanent delete has no inverse, and saying otherwise would be a
        // lie the user only discovers when they need it.
        return;
    }

    m_undoStack.append(entry);
    Q_EMIT historyChanged();
}

void FileOperations::undo()
{
    if (m_undoStack.isEmpty())
        return;

    const UndoEntry entry = m_undoStack.takeLast();
    Q_EMIT historyChanged();
    Pending pending;
    pending.request = entry.inverse;
    pending.isUndo = true;
    pending.redoLabel = entry.label;
    pending.redoRequest = entry.redo;
    enqueuePending(pending);
}

void FileOperations::redo()
{
    if (m_redoStack.isEmpty())
        return;

    const RedoEntry entry = m_redoStack.takeLast();
    Q_EMIT historyChanged();
    // A redo runs as a normal operation (so it re-records its undo) but must
    // not clear the rest of the redo stack — several undos stay replayable
    // in order.
    Pending pending;
    pending.request = entry.request;
    pending.isRedo = true;
    enqueuePending(pending);
}

void FileOperations::cancel()
{
    m_queue.clear();
    if (m_worker)
        m_worker->requestCancel();
    Q_EMIT operationsChanged();
}

void FileOperations::createFolder(const QString &parentDir, const QString &name)
{
    FileOperationRequest request;
    request.kind = FileOperationRequest::CreateFolder;
    request.sources = { name };
    request.destination = parentDir;
    enqueue(request);
}

void FileOperations::rename(const QString &path, const QString &newName)
{
    if (newName.isEmpty() || newName.contains(QLatin1Char('/'))) {
        setError(QStringLiteral("A name cannot be empty or contain “/”"));
        return;
    }

    FileOperationRequest request;
    request.kind = FileOperationRequest::Rename;
    request.sources = { path };
    request.destination = newName;
    enqueue(request);
}

void FileOperations::batchRename(const QStringList &paths, const QStringList &newNames)
{
    if (paths.isEmpty() || paths.size() != newNames.size()) {
        setError(QStringLiteral("Nothing to rename"));
        return;
    }
    for (const QString &name : newNames) {
        if (name.isEmpty() || name.contains(QLatin1Char('/'))) {
            setError(QStringLiteral("A name cannot be empty or contain “/”"));
            return;
        }
    }

    FileOperationRequest request;
    request.kind = FileOperationRequest::BatchRename;
    request.sources = paths;
    request.names = newNames;
    enqueue(request);
}

void FileOperations::createLink(const QStringList &paths, const QString &destinationDir)
{
    if (paths.isEmpty() || destinationDir.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::CreateLink;
    request.sources = paths;
    request.destination = destinationDir;
    enqueue(request);
}

void FileOperations::compress(const QStringList &paths, const QString &archivePath,
                              const QString &password)
{
    if (paths.isEmpty() || archivePath.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::Compress;
    request.sources = paths;
    request.destination = archivePath;
    request.password = password;
    enqueue(request);
}

// An extract hit an encrypted archive: the request parks here while the
// window asks for the password, then replays with it — or is dropped.
void FileOperations::providePassphrase(const QString &password)
{
    if (!m_awaitingPassphrase)
        return;
    m_awaitingPassphrase = false;
    FileOperationRequest request = m_passphraseRequest;
    m_passphraseRequest = FileOperationRequest();
    request.password = password;
    Pending pending;
    pending.request = request;
    enqueuePending(pending);
}

void FileOperations::declinePassphrase()
{
    m_awaitingPassphrase = false;
    m_passphraseRequest = FileOperationRequest();
}

void FileOperations::extractHere(const QStringList &archivePaths, const QString &destinationDir)
{
    if (archivePaths.isEmpty() || destinationDir.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::Extract;
    request.sources = archivePaths;
    request.destination = destinationDir;
    enqueue(request);
}

void FileOperations::trash(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::Trash;
    request.sources = paths;
    enqueue(request);
}

void FileOperations::deletePermanently(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::DeletePermanently;
    request.sources = paths;
    enqueue(request);
}

void FileOperations::restoreFromTrash(const QStringList &originalPaths)
{
    if (originalPaths.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::RestoreFromTrash;
    request.sources = originalPaths;
    enqueue(request);
}

void FileOperations::emptyTrash()
{
    FileOperationRequest request;
    request.kind = FileOperationRequest::EmptyTrash;
    enqueue(request);
}

void FileOperations::copy(const QStringList &paths, const QString &destinationDir, int policy)
{
    if (paths.isEmpty() || destinationDir.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::Copy;
    request.sources = paths;
    request.destination = destinationDir;
    request.policy = ConflictPolicy(policy);
    enqueue(request);
}

void FileOperations::move(const QStringList &paths, const QString &destinationDir, int policy)
{
    if (paths.isEmpty() || destinationDir.isEmpty())
        return;
    FileOperationRequest request;
    request.kind = FileOperationRequest::Move;
    request.sources = paths;
    request.destination = destinationDir;
    request.policy = ConflictPolicy(policy);
    enqueue(request);
}
