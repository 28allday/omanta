#include "ArchiveEngine.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <archive.h>
#include <archive_entry.h>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ArchiveEngine {

namespace {

QString archiveError(struct archive *a, const char *fallback)
{
    const char *message = a ? archive_error_string(a) : nullptr;
    return QString::fromUtf8(message && *message ? message : fallback);
}

// One file or directory headed into an archive: where it is on disk and the
// relative path it carries inside.
struct Source {
    QString absolute;
    QString relative;
};

// The selection plus everything under it, relative paths rooted at the
// selection's parent — so extracting reproduces what was selected, not the
// absolute paths it came from.
bool collectSources(const QStringList &paths, QList<Source> &sources, qint64 *totalBytes,
                    QString *error)
{
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            *error = QStringLiteral("“%1” does not exist").arg(info.fileName());
            return false;
        }
        sources.append({ info.absoluteFilePath(), info.fileName() });
        if (info.isFile() && !info.isSymLink())
            *totalBytes += info.size();

        if (info.isDir() && !info.isSymLink()) {
            const QString base = info.absolutePath();
            QDirIterator it(info.absoluteFilePath(),
                            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo child(it.nextFileInfo());
                sources.append({ child.absoluteFilePath(),
                                 QDir(base).relativeFilePath(child.absoluteFilePath()) });
                if (child.isFile() && !child.isSymLink())
                    *totalBytes += child.size();
            }
        }
    }
    return true;
}

bool writeEntry(struct archive *writer, const Source &source, QString *error,
                const Cancelled &cancelled, const Progress &progress, qint64 *done,
                qint64 totalBytes)
{
    // lstat, not stat: a symlink is archived as a symlink, never as a copy of
    // whatever it currently points to.
    struct stat st;
    if (lstat(source.absolute.toLocal8Bit().constData(), &st) != 0) {
        *error = QStringLiteral("Could not read “%1”").arg(source.relative);
        return false;
    }

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, source.relative.toUtf8().constData());
    archive_entry_copy_stat(entry, &st);
    if (S_ISLNK(st.st_mode)) {
        // The raw readlink value, not QFile::symLinkTarget — Qt resolves the
        // target to an absolute path, which would turn every relative link
        // into one pointing back into the tree it was archived from.
        char target[PATH_MAX + 1] = {};
        const ssize_t len = readlink(source.absolute.toLocal8Bit().constData(), target,
                                     PATH_MAX);
        if (len < 0) {
            *error = QStringLiteral("Could not read “%1”").arg(source.relative);
            archive_entry_free(entry);
            return false;
        }
        archive_entry_set_symlink(entry, target);
        archive_entry_set_size(entry, 0);
    }

    if (archive_write_header(writer, entry) != ARCHIVE_OK) {
        *error = archiveError(writer, "Could not write the archive");
        archive_entry_free(entry);
        return false;
    }

    if (S_ISREG(st.st_mode)) {
        QFile file(source.absolute);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Could not read “%1”").arg(source.relative);
            archive_entry_free(entry);
            return false;
        }
        char buffer[128 * 1024];
        while (true) {
            if (cancelled()) {
                *error = QStringLiteral("Cancelled");
                archive_entry_free(entry);
                return false;
            }
            const qint64 got = file.read(buffer, sizeof buffer);
            if (got < 0) {
                *error = QStringLiteral("Could not read “%1”").arg(source.relative);
                archive_entry_free(entry);
                return false;
            }
            if (got == 0)
                break;
            if (archive_write_data(writer, buffer, size_t(got)) < 0) {
                *error = archiveError(writer, "Could not write the archive");
                archive_entry_free(entry);
                return false;
            }
            *done += got;
            progress(*done, totalBytes);
        }
    }

    archive_entry_free(entry);
    return true;
}

// "photos" → "photos 2" → "photos 3", Nautilus's extraction naming, applied
// to whatever the archive wants to land as.
QString uniqueTarget(const QString &directory, const QString &name)
{
    if (!QFileInfo::exists(QDir(directory).filePath(name)))
        return name;
    const int extAt = archiveExtensionOffset(name) >= 0
        ? -1 // archive names never reach here, but stay honest about the API
        : name.lastIndexOf(QLatin1Char('.'));
    const QString stem = extAt > 0 ? name.left(extAt) : name;
    const QString suffix = extAt > 0 ? name.mid(extAt) : QString();
    for (int n = 2; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1 %2%3").arg(stem).arg(n).arg(suffix);
        if (!QFileInfo::exists(QDir(directory).filePath(candidate)))
            return candidate;
    }
    return name;
}

// An entry path a hostile archive controls must not be able to leave the
// staging directory: no absolute paths, no "..".
bool safeEntryPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')))
        return false;
    const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'));
    return !parts.contains(QStringLiteral(".."));
}

} // namespace

int archiveExtensionOffset(const QString &name)
{
    static const QStringList suffixes = {
        // Compound first, so ".tar.gz" wins over ".gz".
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.bz2"),
        QStringLiteral(".tar.zst"), QStringLiteral(".tar.lz4"), QStringLiteral(".tar.Z"),
        QStringLiteral(".tgz"), QStringLiteral(".txz"), QStringLiteral(".tbz2"),
        QStringLiteral(".zip"), QStringLiteral(".7z"), QStringLiteral(".tar"),
        QStringLiteral(".rar"), QStringLiteral(".cpio"),
        QStringLiteral(".gz"), QStringLiteral(".xz"), QStringLiteral(".zst"),
        QStringLiteral(".bz2"),
    };
    for (const QString &suffix : suffixes) {
        if (name.length() > suffix.length() && name.endsWith(suffix, Qt::CaseInsensitive))
            return name.length() - suffix.length();
    }
    return -1;
}

bool isArchiveContentType(const QString &contentType)
{
    static const QSet<QString> types = {
        QStringLiteral("application/zip"),
        QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/x-tar"),
        QStringLiteral("application/x-compressed-tar"),
        QStringLiteral("application/x-bzip-compressed-tar"),
        QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-zstd-compressed-tar"),
        QStringLiteral("application/x-lzma-compressed-tar"),
        QStringLiteral("application/x-lz4-compressed-tar"),
        QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-cpio"),
        QStringLiteral("application/gzip"),
        QStringLiteral("application/x-xz"),
        QStringLiteral("application/zstd"),
        QStringLiteral("application/x-bzip2"),
    };
    return types.contains(contentType);
}

bool compress(const QStringList &sources, const QString &archivePath, QString *error,
              const Cancelled &cancelled, const Progress &progress,
              const QString &password)
{
    if (sources.isEmpty()) {
        *error = QStringLiteral("Nothing to compress");
        return false;
    }
    if (QFileInfo::exists(archivePath)) {
        // The dialog validates first, so reaching this means a race — refuse
        // rather than clobber whatever appeared.
        *error = QStringLiteral("“%1” already exists").arg(QFileInfo(archivePath).fileName());
        return false;
    }

    struct archive *writer = archive_write_new();
    bool formatOk = true;
    if (archivePath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        formatOk = archive_write_set_format_zip(writer) == ARCHIVE_OK;
    } else if (archivePath.endsWith(QStringLiteral(".tar.xz"), Qt::CaseInsensitive)) {
        formatOk = archive_write_set_format_pax_restricted(writer) == ARCHIVE_OK
            && archive_write_add_filter_xz(writer) == ARCHIVE_OK;
    } else if (archivePath.endsWith(QStringLiteral(".7z"), Qt::CaseInsensitive)) {
        formatOk = archive_write_set_format_7zip(writer) == ARCHIVE_OK;
    } else {
        *error = QStringLiteral("Unsupported archive format");
        archive_write_free(writer);
        return false;
    }
    if (!formatOk) {
        *error = archiveError(writer, "Could not set up the archive format");
        archive_write_free(writer);
        return false;
    }

    // Encrypted zip: zipcrypt, what Nautilus (autoar) writes — weak but
    // universally readable, incl. Windows Explorer. Only the zip writer
    // accepts a passphrase here; the dialog only offers it for zip.
    if (!password.isEmpty()) {
        if (!archivePath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            *error = QStringLiteral("Only zip archives can be encrypted");
            archive_write_free(writer);
            return false;
        }
        if (archive_write_set_options(writer, "zip:encryption=zipcrypt") != ARCHIVE_OK
            || archive_write_set_passphrase(writer, password.toUtf8().constData())
               != ARCHIVE_OK) {
            *error = archiveError(writer, "Could not set up encryption");
            archive_write_free(writer);
            return false;
        }
    }

    QList<Source> entries;
    qint64 totalBytes = 0;
    if (!collectSources(sources, entries, &totalBytes, error)) {
        archive_write_free(writer);
        return false;
    }

    if (archive_write_open_filename(writer, archivePath.toLocal8Bit().constData())
        != ARCHIVE_OK) {
        *error = archiveError(writer, "Could not create the archive");
        archive_write_free(writer);
        return false;
    }

    bool ok = true;
    qint64 done = 0;
    for (const Source &source : entries) {
        if (cancelled()) {
            *error = QStringLiteral("Cancelled");
            ok = false;
            break;
        }
        if (!writeEntry(writer, source, error, cancelled, progress, &done, totalBytes)) {
            ok = false;
            break;
        }
    }

    archive_write_close(writer);
    archive_write_free(writer);

    // A failed or cancelled compression leaves no half-written archive to be
    // mistaken for a good one later.
    if (!ok)
        QFile::remove(archivePath);
    return ok;
}

bool extract(const QString &archivePath, const QString &destinationDir, QString *produced,
             QString *error, const Cancelled &cancelled, const Progress &progress,
             const QString &password, bool *needsPassphrase)
{
    if (needsPassphrase)
        *needsPassphrase = false;
    const QFileInfo archiveInfo(archivePath);
    const qint64 archiveBytes = archiveInfo.size();
    const int extAt = archiveExtensionOffset(archiveInfo.fileName());
    const QString stem = extAt > 0 ? archiveInfo.fileName().left(extAt)
                                   : archiveInfo.fileName();

    struct archive *reader = archive_read_new();
    archive_read_support_format_all(reader);
    archive_read_support_filter_all(reader);
    if (!password.isEmpty())
        archive_read_add_passphrase(reader, password.toUtf8().constData());

    // A bare .gz/.xz/.zst/.bz2 (not a .tar.*) holds one nameless stream, which
    // only the "raw" pseudo-format can read. Raw is enabled ONLY for those
    // names — enabled globally it would accept any corrupt file as a one-entry
    // archive of garbage.
    const QString lower = archiveInfo.fileName().toLower();
    const bool rawSingle = !lower.contains(QStringLiteral(".tar."))
        && (lower.endsWith(QStringLiteral(".gz")) || lower.endsWith(QStringLiteral(".xz"))
            || lower.endsWith(QStringLiteral(".zst")) || lower.endsWith(QStringLiteral(".bz2")));
    if (rawSingle)
        archive_read_support_format_raw(reader);

    if (archive_read_open_filename(reader, archivePath.toLocal8Bit().constData(), 128 * 1024)
        != ARCHIVE_OK) {
        *error = archiveError(reader, "Could not open the archive");
        archive_read_free(reader);
        return false;
    }

    // Everything lands in a hidden staging directory first: a failed or
    // hostile archive leaves nothing visible behind, and the landing rule can
    // look at what actually came out rather than trusting the entry list.
    const QString staging = QDir(destinationDir)
        .filePath(QStringLiteral(".omanta-extract-%1").arg(quintptr(reader), 0, 16));
    if (!QDir().mkpath(staging)) {
        *error = QStringLiteral("Could not write to “%1”")
            .arg(QFileInfo(destinationDir).fileName());
        archive_read_free(reader);
        return false;
    }

    const auto bail = [&](const QString &message) {
        *error = message;
        // libarchive reports both "Passphrase required for this entry" and
        // "Incorrect passphrase" — either way the fix is a (new) password,
        // not an error dialog.
        if (needsPassphrase && message.contains(QStringLiteral("assphrase")))
            *needsPassphrase = true;
        archive_read_free(reader);
        QDir(staging).removeRecursively();
        return false;
    };

    const int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM
        | ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    while (true) {
        if (cancelled())
            return bail(QStringLiteral("Cancelled"));

        struct archive_entry *entry = nullptr;
        const int status = archive_read_next_header(reader, &entry);
        if (status == ARCHIVE_EOF)
            break;
        if (status < ARCHIVE_OK)
            return bail(archiveError(reader, "Could not read the archive"));

        const char *rawPath = archive_entry_pathname(entry);
        // The raw format's single entry is nameless ("data") — it lands as
        // the archive's stem: notes.txt.gz extracts to notes.txt.
        QString path = rawSingle ? stem : QString::fromUtf8(rawPath ? rawPath : "");
        if (!safeEntryPath(path))
            return bail(QStringLiteral("The archive contains an unsafe path — refusing"));
        archive_entry_set_pathname(
            entry, QDir(staging).filePath(QDir::cleanPath(path)).toUtf8().constData());

        // Hardlink targets are entry paths too, and must stay inside staging.
        if (const char *hardlink = archive_entry_hardlink(entry)) {
            const QString linkPath = QString::fromUtf8(hardlink);
            if (!safeEntryPath(linkPath))
                return bail(QStringLiteral("The archive contains an unsafe path — refusing"));
            archive_entry_set_hardlink(
                entry, QDir(staging).filePath(QDir::cleanPath(linkPath)).toUtf8().constData());
        }

        if (archive_read_extract(reader, entry, flags) < ARCHIVE_OK)
            return bail(archiveError(reader, "Could not extract the archive"));

        progress(archive_filter_bytes(reader, -1), archiveBytes);
    }
    archive_read_free(reader);

    const QFileInfoList topLevel = QDir(staging).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (topLevel.isEmpty()) {
        QDir(staging).removeRecursively();
        *error = QStringLiteral("The archive is empty");
        return false;
    }

    // Nautilus's landing rule: one top-level entry comes out as itself; more
    // than one gets a folder named after the archive. Never overwrite — the
    // target is unique-ified either way.
    QString finalPath;
    if (topLevel.size() == 1) {
        const QString name = uniqueTarget(destinationDir, topLevel.first().fileName());
        finalPath = QDir(destinationDir).filePath(name);
        if (!QFile::rename(topLevel.first().absoluteFilePath(), finalPath)) {
            QDir(staging).removeRecursively();
            *error = QStringLiteral("Could not move the extracted files into place");
            return false;
        }
    } else {
        const QString name = uniqueTarget(destinationDir, stem);
        finalPath = QDir(destinationDir).filePath(name);
        if (!QDir().mkpath(finalPath)) {
            QDir(staging).removeRecursively();
            *error = QStringLiteral("Could not move the extracted files into place");
            return false;
        }
        for (const QFileInfo &item : topLevel) {
            if (!QFile::rename(item.absoluteFilePath(),
                               QDir(finalPath).filePath(item.fileName()))) {
                QDir(staging).removeRecursively();
                *error = QStringLiteral("Could not move the extracted files into place");
                return false;
            }
        }
    }

    QDir(staging).removeRecursively();
    *produced = finalPath;
    return true;
}

} // namespace ArchiveEngine
