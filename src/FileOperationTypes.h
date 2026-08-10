#pragma once

#include <QMetaType>
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
        RemoveCreatedFolder
    };

    Kind kind = Copy;
    QStringList sources;
    QString destination;
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
    QStringList produced; // paths this operation created
    QStringList sources;  // paths it consumed or moved from
    QStringList skipped;  // paths deliberately left alone
};

Q_DECLARE_METATYPE(FileOperationRequest)
Q_DECLARE_METATYPE(FileOperationResult)
