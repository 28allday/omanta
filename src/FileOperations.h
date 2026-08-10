#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QVariantList>
#include <QStringList>
#include <QThread>
#include <QtQmlIntegration>

#include "FileOperationTypes.h"

class FileOperationWorker;

// The main-thread face of every write the app performs, and the owner of the
// undo journal.
//
// The journal is built as operations complete rather than bolted on later,
// because an inverse can only be recorded by the code that knows what actually
// happened — which files a copy created after conflict renaming, where a move
// came from. Reconstructing that afterwards is guesswork, and guesswork here
// deletes the wrong file.
class FileOperations : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY progressChanged)
    // The progress popover's model: one map per live operation — the running
    // one first ({id, label, state, progress, detail, remaining}), then the
    // queue in order. Empty when idle.
    Q_PROPERTY(QVariantList operations READ operations NOTIFY operationsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    // Mirrors ConflictPolicy for QML, which cannot see a scoped enum.
    enum Conflict {
        RenameNew = int(ConflictPolicy::RenameNew),
        Replace = int(ConflictPolicy::Replace),
        Skip = int(ConflictPolicy::Skip),
    };
    Q_ENUM(Conflict)

    explicit FileOperations(QObject *parent = nullptr);
    ~FileOperations() override;

    bool busy() const { return m_busy; }
    qreal progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    QVariantList operations() const;

    // "About 2 minutes left" from bytes done/total and elapsed time; empty
    // until a rate is worth trusting. Static and public so the arithmetic is
    // unit-testable without staging a slow copy.
    static QString remainingText(qint64 done, qint64 total, qint64 elapsedMs);
    // "1.2 GB / 10 GB (10.3 MB/s)" — the byte-level details line. Same
    // static-for-testability shape as remainingText.
    static QString transferredText(qint64 done, qint64 total, qint64 elapsedMs);
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    QString undoLabel() const;
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    QString redoLabel() const;
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void createFolder(const QString &parentDir, const QString &name);
    Q_INVOKABLE void rename(const QString &path, const QString &newName);

    // Renames paths[i] to newNames[i] as ONE undoable step. Order-safe (swaps
    // and shifts go through temp names) and all-or-nothing: a mid-batch
    // failure rolls the completed renames back.
    Q_INVOKABLE void batchRename(const QStringList &paths, const QStringList &newNames);
    Q_INVOKABLE void trash(const QStringList &paths);

    // "Link to <name>" symlinks in destinationDir, one per path — Nautilus's
    // optional Create Link action. Taken names get the "(copy)" suffixing.
    // Undo deletes the links; the targets were never the operation's to touch.
    Q_INVOKABLE void createLink(const QStringList &paths, const QString &destinationDir);

    // Archive the paths into archivePath (.zip / .tar.xz / .7z by extension).
    // Undo deletes the archive. Refuses to overwrite an existing file.
    // A non-empty password writes an encrypted zip (zip only).
    Q_INVOKABLE void compress(const QStringList &paths, const QString &archivePath,
                              const QString &password = QString());

    // The passphraseNeeded flow: an extract hit an encrypted archive, the
    // request is parked; the window asks, then either replays it with the
    // password or drops it.
    Q_INVOKABLE void providePassphrase(const QString &password);
    Q_INVOKABLE void declinePassphrase();

    // Extract each archive into destinationDir with Nautilus's landing rule
    // (single top-level entry as itself, else a folder named after the
    // archive), never overwriting. Undo deletes what was extracted.
    Q_INVOKABLE void extractHere(const QStringList &archivePaths, const QString &destinationDir);
    Q_INVOKABLE void copy(const QStringList &paths, const QString &destinationDir,
                          int policy = RenameNew);
    Q_INVOKABLE void move(const QStringList &paths, const QString &destinationDir,
                          int policy = RenameNew);

    // No undo. Named so nobody calls it by accident.
    Q_INVOKABLE void deletePermanently(const QStringList &paths);

    // Put trashed items back where they came from. Takes ORIGINAL paths (the
    // trash:// rows' trash::orig-path), not trash:// URIs. Undoable: the
    // inverse trashes them again.
    Q_INVOKABLE void restoreFromTrash(const QStringList &originalPaths);

    // Deletes everything in trash:///. No undo — the UI confirms first.
    Q_INVOKABLE void emptyTrash();

    // Cancel one operation from the popover: the running one is interrupted,
    // a queued one is quietly dropped before it starts.
    Q_INVOKABLE void cancelOperation(double id);

    Q_INVOKABLE void undo();
    // Replays the undone operation — the original request, verbatim, through
    // the same conflict machinery, so a redo can never overwrite anything the
    // original could not. Any fresh operation clears the redo stack.
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void busyChanged();
    void progressChanged();
    void operationsChanged();
    void historyChanged();
    void lastErrorChanged();
    void operationFinished(const QString &label);
    // An extract needs a password; the window shows the prompt.
    void passphraseNeeded(const QString &archiveName);

    // Internal: hands work to the worker thread.
    void dispatch(const FileOperationRequest &request, quint64 id);

private Q_SLOTS:
    void handleProgress(quint64 id, qint64 done, qint64 total, const QString &currentName);
    void handleSuccess(quint64 id, const FileOperationResult &result);
    void handleFailure(quint64 id, const QString &message);
    void handlePassphraseNeeded(quint64 id, const QString &archiveName);

private:
    struct Pending {
        quint64 id = 0;
        FileOperationRequest request;
        bool isUndo = false;
        bool isRedo = false;
        // Set on an undo: what becomes redoable once the undo succeeds. A
        // failed undo must offer no redo of something still done.
        QString redoLabel;
        FileOperationRequest redoRequest;
    };

    struct UndoEntry {
        QString label;
        FileOperationRequest inverse;
        FileOperationRequest redo; // the original request, replayed by redo()
    };

    struct RedoEntry {
        QString label;
        FileOperationRequest request;
    };

    void enqueue(const FileOperationRequest &request);
    void enqueuePending(Pending pending);
    void startNext();
    void recordUndo(const FileOperationRequest &request, const FileOperationResult &result);
    void setBusy(bool busy);
    void setStatus(const QString &text, qreal progress);
    void setError(const QString &message);

    QThread m_thread;
    FileOperationWorker *m_worker = nullptr;

    QQueue<Pending> m_queue;
    Pending m_current;
    // The extract parked while the window asks for its password.
    bool m_awaitingPassphrase = false;
    FileOperationRequest m_passphraseRequest;
    bool m_busy = false;
    quint64 m_nextId = 1;

    qreal m_progress = 0.0;
    QString m_statusText;
    QString m_lastError;

    // The running operation's popover facts, reset by startNext.
    QString m_currentDetail;
    qint64 m_currentDone = 0;
    qint64 m_currentTotal = 0;
    QElapsedTimer m_currentClock;

    QList<UndoEntry> m_undoStack;
    QList<RedoEntry> m_redoStack;
};
