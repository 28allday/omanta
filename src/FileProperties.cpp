#include "FileProperties.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <unistd.h>

namespace {

// One query answers the whole Basic tab and the whole Permissions tab. Asking
// for these separately would be a syscall each, per file, per dialog.
const char *kAttributes =
    G_FILE_ATTRIBUTE_STANDARD_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_SIZE ","
    G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE ","
    G_FILE_ATTRIBUTE_STANDARD_ICON ","
    G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
    G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
    G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET ","
    G_FILE_ATTRIBUTE_TIME_MODIFIED ","
    G_FILE_ATTRIBUTE_TIME_ACCESS ","
    G_FILE_ATTRIBUTE_TIME_CREATED ","
    G_FILE_ATTRIBUTE_UNIX_MODE ","
    G_FILE_ATTRIBUTE_UNIX_UID ","
    G_FILE_ATTRIBUTE_UNIX_GID ","
    G_FILE_ATTRIBUTE_OWNER_USER ","
    G_FILE_ATTRIBUTE_OWNER_GROUP;

const char *kFilesystemAttributes =
    G_FILE_ATTRIBUTE_FILESYSTEM_TYPE ","
    G_FILE_ATTRIBUTE_FILESYSTEM_FREE ","
    G_FILE_ATTRIBUTE_FILESYSTEM_SIZE;

QDateTime timeFrom(GFileInfo *info, const char *attribute)
{
    if (!g_file_info_has_attribute(info, attribute))
        return {};
    const guint64 seconds = g_file_info_get_attribute_uint64(info, attribute);
    if (seconds == 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(qint64(seconds));
}

QString stringFrom(GFileInfo *info, const char *attribute)
{
    const char *value = g_file_info_get_attribute_string(info, attribute);
    return value ? QString::fromUtf8(value) : QString();
}

} // namespace

FileProperties::FileProperties(QObject *parent)
    : QObject(parent)
{
}

FileProperties::~FileProperties()
{
    cancelInFlight();
}

void FileProperties::setPaths(const QStringList &paths)
{
    if (paths == m_paths)
        return;
    m_paths = paths;
    Q_EMIT pathsChanged();
    load();
}

void FileProperties::reload()
{
    load();
}

void FileProperties::cancelInFlight()
{
    if (!m_cancellable)
        return;
    g_cancellable_cancel(m_cancellable);
    g_object_unref(m_cancellable);
    m_cancellable = nullptr;
}

void FileProperties::reset()
{
    m_loaded = false;
    m_errorMessage.clear();
    m_displayName.clear();
    m_iconNames.clear();
    m_contentType.clear();
    m_typeDescription.clear();
    m_location.clear();
    m_isDir = false;
    m_isSymlink = false;
    m_symlinkTarget.clear();
    m_modified = {};
    m_accessed = {};
    m_created = {};
    m_owner.clear();
    m_group.clear();
    m_size = 0;
    m_sizeOnDisk = 0;
    m_fileCount = 0;
    m_folderCount = 0;
    m_liveSize = 0;
    m_liveFiles = 0;
    m_liveFolders = 0;
    m_measuring = false;
    m_mode = -1;
    m_canChangeMode = false;
    m_filesystemType.clear();
    m_filesystemFree = 0;
    m_filesystemSize = 0;
    m_toMeasure.clear();
    m_outstandingInfo = 0;
}

void FileProperties::load()
{
    ++m_generation;
    cancelInFlight();
    reset();
    Q_EMIT infoChanged();
    Q_EMIT measureChanged();
    Q_EMIT filesystemChanged();

    if (m_paths.isEmpty())
        return;

    m_cancellable = g_cancellable_new();
    m_outstandingInfo = int(m_paths.size());

    for (int index = 0; index < m_paths.size(); ++index) {
        GFile *file = g_file_new_for_path(m_paths.at(index).toUtf8().constData());
        auto *ctx = new CallbackCtx{ this, m_generation, index };

        // Symlinks are followed: what the user wants to know about a link is
        // mostly what it points at. GIO still reports is-symlink and the target
        // on a followed query, so one query answers both.
        g_file_query_info_async(file, kAttributes, G_FILE_QUERY_INFO_NONE,
                                G_PRIORITY_DEFAULT, m_cancellable,
                                &FileProperties::onInfoReady, ctx);

        if (index == 0) {
            g_file_query_filesystem_info_async(file, kFilesystemAttributes,
                                               G_PRIORITY_DEFAULT, m_cancellable,
                                               &FileProperties::onFilesystemReady,
                                               new CallbackCtx{ this, m_generation, 0 });
        }

        g_object_unref(file);
    }
}

void FileProperties::onInfoReady(GObject *source, GAsyncResult *res, gpointer data)
{
    std::unique_ptr<CallbackCtx> ctx(static_cast<CallbackCtx *>(data));

    GError *error = nullptr;
    GFileInfo *info = g_file_query_info_finish(G_FILE(source), res, &error);

    FileProperties *self = ctx->self;
    if (!self || ctx->generation != self->m_generation) {
        if (info)
            g_object_unref(info);
        g_clear_error(&error);
        return;
    }

    if (!info) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            self->setError(QString::fromUtf8(error ? error->message : "Could not read the file"));
        g_clear_error(&error);
        if (--self->m_outstandingInfo == 0) {
            self->m_loaded = true;
            Q_EMIT self->infoChanged();
            self->startNextMeasure();
        }
        return;
    }
    g_clear_error(&error);

    if (ctx->index == 0)
        self->applyPrimaryInfo(info);
    self->accountFor(info, ctx->index);

    g_object_unref(info);

    if (--self->m_outstandingInfo == 0) {
        self->m_loaded = true;
        Q_EMIT self->infoChanged();
        Q_EMIT self->measureChanged();
        self->startNextMeasure();
    }
}

void FileProperties::applyPrimaryInfo(GFileInfo *info)
{
    const QString path = m_paths.constFirst();

    if (const char *display = g_file_info_get_display_name(info))
        m_displayName = QString::fromUtf8(display);
    else
        m_displayName = QFileInfo(path).fileName();

    m_isDir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    m_isSymlink = g_file_info_get_attribute_boolean(info,
                                                    G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK);

    // The getter is not a query — it asserts the attribute is present and
    // logs a CRITICAL when it is not, which is every ordinary file.
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET)) {
        if (const char *target = g_file_info_get_symlink_target(info))
            m_symlinkTarget = QString::fromUtf8(target);
    }

    m_location = QFileInfo(path).absolutePath();

    if (const char *type = g_file_info_get_content_type(info)) {
        m_contentType = QString::fromUtf8(type);
        if (char *description = g_content_type_get_description(type)) {
            m_typeDescription = QString::fromUtf8(description);
            g_free(description);
        }
    }

    if (GIcon *icon = g_file_info_get_icon(info)) {
        if (G_IS_THEMED_ICON(icon)) {
            const gchar *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            for (int i = 0; names && names[i]; ++i)
                m_iconNames.append(QString::fromUtf8(names[i]));
        }
    }

    m_modified = timeFrom(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    m_accessed = timeFrom(info, G_FILE_ATTRIBUTE_TIME_ACCESS);
    m_created = timeFrom(info, G_FILE_ATTRIBUTE_TIME_CREATED);

    m_owner = stringFrom(info, G_FILE_ATTRIBUTE_OWNER_USER);
    m_group = stringFrom(info, G_FILE_ATTRIBUTE_OWNER_GROUP);

    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE))
        m_mode = int(g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE) & 07777);

    // chmod is the owner's privilege, not the writer's — a file you can write
    // to in a world-writable folder is still not yours to re-permission.
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_UID)) {
        const uid_t uid = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_UID);
        const uid_t effective = geteuid();
        m_canChangeMode = m_mode >= 0 && m_paths.size() == 1
                          && (uid == effective || effective == 0);
    }
}

void FileProperties::accountFor(GFileInfo *info, int index)
{
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
        // Queued for a recursive walk, and counted only when that finishes, so
        // the number never runs ahead of what has actually been looked at.
        // Which paths are directories is taken from the info that already came
        // back rather than a fresh stat — the answer is in hand.
        if (index >= 0 && index < m_paths.size())
            m_toMeasure.append(m_paths.at(index));
        return;
    }

    m_size += qint64(g_file_info_get_size(info));
    ++m_fileCount;

    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE)) {
        m_sizeOnDisk += qint64(
            g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE));
    }
}

void FileProperties::startNextMeasure()
{
    if (m_toMeasure.isEmpty()) {
        if (m_measuring) {
            m_measuring = false;
            Q_EMIT measureChanged();
        }
        return;
    }

    const QString next = m_toMeasure.takeFirst();
    m_measuring = true;
    m_liveSize = 0;
    m_liveFiles = 0;
    m_liveFolders = 0;
    Q_EMIT measureChanged();

    GFile *file = g_file_new_for_path(next.toUtf8().constData());
    auto *ctx = new CallbackCtx{ this, m_generation, 0 };

    // APPARENT_SIZE is the number a user recognises as "the size of this
    // folder"; NO_XDEV stops a walk of $HOME wandering into every mounted disk
    // under it and reporting a total nobody asked for.
    g_file_measure_disk_usage_async(file,
                                    GFileMeasureFlags(G_FILE_MEASURE_APPARENT_SIZE
                                                      | G_FILE_MEASURE_NO_XDEV),
                                    G_PRIORITY_LOW, m_cancellable,
                                    &FileProperties::onMeasureProgress, ctx,
                                    &FileProperties::onMeasureReady, ctx);

    g_object_unref(file);
}

void FileProperties::onMeasureProgress(gboolean reporting, guint64 currentSize, guint64 numDirs,
                                       guint64 numFiles, gpointer data)
{
    auto *ctx = static_cast<CallbackCtx *>(data);

    FileProperties *self = ctx->self;
    if (!self || ctx->generation != self->m_generation)
        return;
    if (!reporting)
        return;

    // GLib marshals this back through the main context that started the walk,
    // so it arrives on the GUI thread and may touch Qt objects directly.
    // Verified on this machine before the class was written.
    self->m_liveSize = qint64(currentSize);
    self->m_liveFiles = int(numFiles);
    self->m_liveFolders = int(numDirs > 0 ? numDirs - 1 : 0);
    Q_EMIT self->measureChanged();
}

void FileProperties::onMeasureReady(GObject *source, GAsyncResult *res, gpointer data)
{
    // The same context served the progress callback, which GIO gives no
    // destroy-notify for — completion is the only place the caller can free it,
    // and GLib queues every progress report ahead of the completion it belongs
    // to, so by here no more can arrive.
    std::unique_ptr<CallbackCtx> ctx(static_cast<CallbackCtx *>(data));

    guint64 usage = 0;
    guint64 dirs = 0;
    guint64 files = 0;
    GError *error = nullptr;
    const bool ok = g_file_measure_disk_usage_finish(G_FILE(source), res, &usage, &dirs, &files,
                                                     &error);

    FileProperties *self = ctx->self;
    if (!self || ctx->generation != self->m_generation) {
        g_clear_error(&error);
        return;
    }

    if (!ok && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        self->setError(QString::fromUtf8(error ? error->message : "Could not measure the folder"));
    g_clear_error(&error);

    self->m_liveSize = 0;
    self->m_liveFiles = 0;
    self->m_liveFolders = 0;

    if (ok) {
        self->m_size += qint64(usage);
        self->m_fileCount += int(files);
        // The folder itself is one of the dirs GIO counted; the user thinks of
        // "contents", so it does not belong in the total.
        self->m_folderCount += int(dirs > 0 ? dirs - 1 : 0);
    }

    Q_EMIT self->measureChanged();
    self->startNextMeasure();
}

void FileProperties::onFilesystemReady(GObject *source, GAsyncResult *res, gpointer data)
{
    std::unique_ptr<CallbackCtx> ctx(static_cast<CallbackCtx *>(data));

    GError *error = nullptr;
    GFileInfo *info = g_file_query_filesystem_info_finish(G_FILE(source), res, &error);
    g_clear_error(&error);

    FileProperties *self = ctx->self;
    if (!self || ctx->generation != self->m_generation) {
        if (info)
            g_object_unref(info);
        return;
    }

    if (!info)
        return; // A filesystem that will not describe itself is not an error worth showing.

    self->m_filesystemType = stringFrom(info, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE);
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE)) {
        self->m_filesystemFree =
            qint64(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE));
    }
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE)) {
        self->m_filesystemSize =
            qint64(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE));
    }

    g_object_unref(info);
    Q_EMIT self->filesystemChanged();
}

void FileProperties::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    Q_EMIT infoChanged();
}

QString FileProperties::iconSource() const
{
    QStringList names = m_iconNames;
    if (names.isEmpty())
        names.append(m_isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic"));
    return QStringLiteral("image://fileicon/") + names.join(QLatin1Char(','));
}

QString FileProperties::modeOctal() const
{
    if (m_mode < 0)
        return {};
    return QStringLiteral("%1").arg(m_mode, 4, 8, QLatin1Char('0'));
}

QString FileProperties::modeToText(int mode)
{
    if (mode < 0)
        return {};

    QString text = QStringLiteral("---------");
    const char *letters = "rwx";
    for (int bit = 0; bit < 9; ++bit) {
        if (mode & (1 << (8 - bit)))
            text[bit] = QLatin1Char(letters[bit % 3]);
    }

    // setuid/setgid/sticky replace the execute letter in their triad, upper
    // case when the execute bit is off — the convention `ls -l` uses.
    const auto special = [&text](int index, int bitSet, char lower, char upper) {
        if (!bitSet)
            return;
        text[index] = QLatin1Char(text[index] == QLatin1Char('x') ? lower : upper);
    };
    special(2, mode & 04000, 's', 'S');
    special(5, mode & 02000, 's', 'S');
    special(8, mode & 01000, 't', 'T');

    return text;
}

QVariantList FileProperties::applications() const
{
    QVariantList result;
    if (m_paths.size() != 1 || m_contentType.isEmpty() || m_isDir)
        return result;

    const QByteArray type = m_contentType.toUtf8();

    QString defaultId;
    if (GAppInfo *fallback = g_app_info_get_default_for_type(type.constData(), FALSE)) {
        if (const char *id = g_app_info_get_id(fallback))
            defaultId = QString::fromUtf8(id);
        g_object_unref(fallback);
    }

    GList *all = g_app_info_get_all_for_type(type.constData());
    for (GList *item = all; item; item = item->next) {
        auto *application = static_cast<GAppInfo *>(item->data);
        const char *id = g_app_info_get_id(application);
        if (!id)
            continue;

        QStringList iconNames;
        if (GIcon *icon = g_app_info_get_icon(application)) {
            if (G_IS_THEMED_ICON(icon)) {
                const gchar *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
                for (int i = 0; names && names[i]; ++i)
                    iconNames.append(QString::fromUtf8(names[i]));
            }
        }
        if (iconNames.isEmpty())
            iconNames.append(QStringLiteral("application-x-executable"));

        result.append(QVariantMap{
            { QStringLiteral("id"), QString::fromUtf8(id) },
            { QStringLiteral("name"), QString::fromUtf8(g_app_info_get_display_name(application)) },
            { QStringLiteral("iconSource"),
              QStringLiteral("image://fileicon/") + iconNames.join(QLatin1Char(',')) },
            { QStringLiteral("isDefault"), QString::fromUtf8(id) == defaultId },
        });
    }
    g_list_free_full(all, g_object_unref);

    return result;
}

namespace {

// Finds the GAppInfo behind a desktop-file id. g_app_info_get_all_for_type is
// the source of the ids the dialog shows, so it is the list to look them up in
// — a desktop id can name an entry that GDesktopAppInfo alone will not load.
GAppInfo *findApplication(const QString &applicationId, const QString &contentType)
{
    const QByteArray wanted = applicationId.toUtf8();
    GList *all = g_app_info_get_all_for_type(contentType.toUtf8().constData());
    GAppInfo *found = nullptr;

    for (GList *item = all; item; item = item->next) {
        auto *application = static_cast<GAppInfo *>(item->data);
        const char *id = g_app_info_get_id(application);
        if (id && wanted == id) {
            found = static_cast<GAppInfo *>(g_object_ref(application));
            break;
        }
    }

    g_list_free_full(all, g_object_unref);
    return found;
}

} // namespace

bool FileProperties::launchWith(const QString &applicationId) const
{
    if (m_paths.isEmpty())
        return false;

    GAppInfo *application = findApplication(applicationId, m_contentType);
    if (!application)
        return false;

    GList *files = nullptr;
    for (const QString &path : m_paths)
        files = g_list_append(files, g_file_new_for_path(path.toUtf8().constData()));

    GError *error = nullptr;
    const bool ok = g_app_info_launch(application, files, nullptr, &error);
    if (!ok) {
        qWarning("omanta: could not launch %s: %s", qUtf8Printable(applicationId),
                 error ? error->message : "unknown");
    }
    g_clear_error(&error);

    g_list_free_full(files, g_object_unref);
    g_object_unref(application);
    return ok;
}

bool FileProperties::setDefaultApplication(const QString &applicationId)
{
    if (m_contentType.isEmpty())
        return false;

    GAppInfo *application = findApplication(applicationId, m_contentType);
    if (!application)
        return false;

    GError *error = nullptr;
    const bool ok = g_app_info_set_as_default_for_type(application,
                                                       m_contentType.toUtf8().constData(), &error);
    if (!ok) {
        setError(QString::fromUtf8(error ? error->message : "Could not set the default application"));
    }
    g_clear_error(&error);
    g_object_unref(application);
    return ok;
}

void FileProperties::applyMode(int mode)
{
    if (m_paths.size() != 1 || mode < 0)
        return;

    GFile *file = g_file_new_for_path(m_paths.constFirst().toUtf8().constData());
    GFileInfo *info = g_file_info_new();
    g_file_info_set_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE, guint32(mode & 07777));

    g_file_set_attributes_async(file, info, G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
                                m_cancellable, &FileProperties::onModeApplied,
                                new CallbackCtx{ this, m_generation, 0 });

    g_object_unref(info);
    g_object_unref(file);
}

void FileProperties::onModeApplied(GObject *source, GAsyncResult *res, gpointer data)
{
    std::unique_ptr<CallbackCtx> ctx(static_cast<CallbackCtx *>(data));

    GFileInfo *info = nullptr;
    GError *error = nullptr;
    const bool ok = g_file_set_attributes_finish(G_FILE(source), res, &info, &error);
    if (info)
        g_object_unref(info);

    FileProperties *self = ctx->self;
    if (!self || ctx->generation != self->m_generation) {
        g_clear_error(&error);
        return;
    }

    if (!ok && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        self->setError(QString::fromUtf8(error ? error->message : "Could not change permissions"));
    g_clear_error(&error);

    // Re-read rather than trusting the write: the kernel silently drops setgid
    // on a file you do not own a matching group for, and the dialog should show
    // what is actually on disk.
    if (ok)
        self->load();
}
