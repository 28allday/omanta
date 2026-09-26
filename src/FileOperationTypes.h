#pragma once

#include <QMetaType>
#include <QList>
#include <QString>
#include <QStringList>

// What to do when a destination already exists.
//
// The default is RenameNew everywhere, deliberately: it is the only policy that
// cannot destroy data. Replace is only ever used because a human chose it.
enum class ConflictPolicy {
    RenameNew, // copy → "file (copy).txt"
    Replace,   // overwrite the existing file
    Skip,      // leave the existing file alone
    Fail,      // internal: undo must restore the exact name without clobbering
};

// Actual transfer entries, including directory containers that were merged.
// A native directory rename is one atomic entry (directory == false).
struct TransferEntry
{
    QString source;
    QString destination;
    bool directory = false;
    bool created = false;
    bool moved = false;
    quint32 mode = 0;
};

// Identity and metadata of an extracted output, captured before publication.
// Undo checks these and never recursively removes a directory.
struct CreatedEntry
{
    QString path;
    quint64 device = 0;
    quint64 inode = 0;
    quint32 mode = 0;
    qint64 size = 0;
    qint64 modifiedNs = 0;
    qint64 changedNs = 0;
    bool directory = false;
};

struct FileOperationRequest
{
    enum Kind {
        CreateFolder,     // destination = parent dir, sources[0] = new name
        Rename,           // sources[0] = path, destination = new name
        Trash,            // sources = paths
        RestoreFromTrash, // sources = ORIGINAL paths to restore back to
        Copy,             // sources = paths, destination = target directory
        Move,             // sources = paths, destination = target directory
        DeletePermanently, // sources = paths. No undo. Ever.
        EmptyTrash,       // no sources: everything in trash:///. No undo either.

        Compress, // sources = paths, destination = the archive's full path
        Extract,  // sources = archive paths, destination = target directory

        // "Link to <name>" symlinks in destination, one per source —
        // Nautilus's optional Create Link action. Undo deletes the links.
        CreateLink,

        // Renames sources[i] to names[i], all in one undoable step. Executes
        // through temporary names when the new and old name sets overlap, so
        // swaps and shifts ("2.jpg"→"3.jpg" while "1.jpg"→"2.jpg") work; on
        // any failure the completed renames are rolled back, so a failed
        // batch leaves every name as it was.
        BatchRename,

        // Undo of CreateFolder. Deletes the folder only while it is still
        // empty, so undoing a mkdir can never destroy files the user has put
        // there since. Trash would be safer still, but trash does not exist on
        // every filesystem — see RemoveCreatedFolder in NOTES.md.
        RemoveCreatedFolder,
        UndoTransfer,
        UndoExtraction
    };

    Kind kind = Copy;
    QStringList sources;
    QString destination;
    QList<TransferEntry> transfers; // UndoTransfer only
    QList<CreatedEntry> created; // UndoExtraction only
    QStringList names; // BatchRename only: the new name per source, parallel
    ConflictPolicy policy = ConflictPolicy::RenameNew;
    // Compress: encrypt the zip with this. Extract: unlock with this.
    // Never appears in describe()/shortStatus() or any log.
    QString password;

    QString describe() const;
    // The live, progressive form for the sidebar indicator — Nautilus's
    // "short status": Copying “name” / Copying 3 files.
    QString shortStatus() const;
};

// What an operation actually did, which is what makes undo possible: you cannot
// invert a copy without knowing which files it created, and "the ones I asked
// for" is not the same list once conflict renaming has happened.
struct FileOperationResult
{
    QList<CreatedEntry> created;
    QList<TransferEntry> transfers;
    bool undoable = true; // replacing existing data cannot be undone
    QStringList produced; // paths this operation created
    QStringList sources;  // paths it consumed or moved from
    QStringList skipped;  // paths deliberately left alone
    QStringList trashUnavailable; // only sources rejected with G_IO_ERROR_NOT_SUPPORTED
};

Q_DECLARE_METATYPE(FileOperationRequest)
Q_DECLARE_METATYPE(FileOperationResult)
