#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Compression and extraction over libarchive, as plain blocking functions the
// operation worker calls on its thread. libarchive rather than gnome-autoar:
// same underlying library Nautilus ends up in, but with direct control of
// progress, cancellation and the staging strategy — and no GObject async to
// bridge. Local paths only; archives on gvfs mounts are out of scope.
namespace ArchiveEngine {

// True to abort. Checked between entries and between data chunks.
using Cancelled = std::function<bool()>;
// bytes done, bytes total (0 while the total is still being counted).
using Progress = std::function<void(qint64, qint64)>;

// The formats the Compress dialog offers, chosen by the archive path's
// extension: .zip, .tar.xz, .7z. A non-empty password produces an encrypted
// zip (zipcrypt, the same traditional encryption Nautilus/autoar writes, so
// Windows Explorer can open it); it is ignored for the other formats.
bool compress(const QStringList &sources, const QString &archivePath,
              QString *error, const Cancelled &cancelled, const Progress &progress,
              const QString &password = QString());

// Extracts with Nautilus's (autoar's) landing rule: an archive with a single
// top-level entry extracts as that entry; anything else lands in a new folder
// named after the archive. Either way the target is unique-ified Nautilus
// style ("name 2", "name 3") rather than overwriting. Everything goes through
// a hidden staging directory first, so a failed extraction leaves nothing
// behind; entry paths are sanitized, so a hostile archive cannot write
// outside the destination. `produced` receives the final top-level path.
// `needsPassphrase` (optional) is set when the failure was a missing or wrong
// password — the caller can ask for one and retry instead of showing an error.
bool extract(const QString &archivePath, const QString &destinationDir,
             QString *produced, QString *error, const Cancelled &cancelled,
             const Progress &progress, const QString &password = QString(),
             bool *needsPassphrase = nullptr);

// Where the archive extension starts in `name` (compound-aware: ".tar.gz" is
// one unit), or -1. Used for the landing folder's name and by the UI to
// suggest archive names.
int archiveExtensionOffset(const QString &name);

// Content types "Extract Here" is offered for — what libarchive here can
// actually open, phrased as the types GIO reports.
bool isArchiveContentType(const QString &contentType);

} // namespace ArchiveEngine
