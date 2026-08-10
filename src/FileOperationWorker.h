#pragma once

#include <QObject>

#include <gio/gio.h>

#include "FileOperationTypes.h"

// Executes one file operation, synchronously, on a worker thread.
//
// Why a thread when the rest of the app uses async GIO on the main thread:
// copying a directory tree is thousands of dependent I/O calls, and expressing
// that as a main-thread callback chain buys nothing — the work is I/O-bound
// either way, and a stalled NFS mount would make the window unresponsive. Here
// a blocked call blocks only this thread, and cancellation stays instant
// because GCancellable is thread-safe.
//
// Nothing in this class touches the model or the UI. It emits, and the facade
// on the main thread decides what that means.
class FileOperationWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileOperationWorker(QObject *parent = nullptr);
    ~FileOperationWorker() override;

    // Callable from any thread — GCancellable is thread-safe, and this is the
    // whole reason Cancel is instant rather than "after the current file".
    void requestCancel();

public Q_SLOTS:
    void run(const FileOperationRequest &request, quint64 id);

Q_SIGNALS:
    void progressed(quint64 id, qint64 done, qint64 total, const QString &currentName);
    void succeeded(quint64 id, const FileOperationResult &result);
    void failed(quint64 id, const QString &message);
    // An extract needs a password (missing or wrong) — instead of failed(),
    // so the window can ask and replay rather than show an error.
    void passphraseNeeded(quint64 id, const QString &archiveName);

private:
    struct PlanItem {
        GFile *source = nullptr;
        GFile *destination = nullptr;
        bool isDirectory = false;
        qint64 size = 0;
    };

    // Each returns false and sets `error` on failure.
    bool doCreateFolder(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doCreateLink(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doRename(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doBatchRename(const FileOperationRequest &request, FileOperationResult &result,
                       QString *error, quint64 id);
    bool doCompress(const FileOperationRequest &request, FileOperationResult &result,
                    QString *error, quint64 id);
    bool doExtract(const FileOperationRequest &request, FileOperationResult &result,
                   QString *error, quint64 id);
    bool doTrash(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doRestore(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doDelete(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doEmptyTrash(const FileOperationRequest &request, FileOperationResult &result, QString *error);
    bool doRemoveCreatedFolder(const FileOperationRequest &request, FileOperationResult &result,
                               QString *error);
    bool doTransfer(const FileOperationRequest &request, bool removeSources,
                    FileOperationResult &result, QString *error, quint64 id);

    bool buildPlan(GFile *source, GFile *destination, ConflictPolicy policy,
                   QList<PlanItem> &plan, qint64 *totalBytes, QStringList *skipped,
                   QString *error);
    bool deleteRecursively(GFile *file, QString *error);

    void resetCancellable();

    GCancellable *m_cancellable = nullptr;
    // Set by doExtract when the failure is a missing/wrong archive password;
    // run() turns it into passphraseNeeded() instead of failed().
    bool m_needsPassphrase = false;
    QString m_passphraseArchive;
};
