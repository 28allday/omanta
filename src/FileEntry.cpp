#include "FileEntry.h"

#include <QUrl>

QString FileEntry::permissionString() const
{
    if (mode == 0)
        return {};

    QString out;
    out.reserve(10);
    out += isDir ? QLatin1Char('d') : isSymlink ? QLatin1Char('l') : QLatin1Char('-');

    const struct { quint32 bit; char ch; } bits[] = {
        { 0400, 'r' }, { 0200, 'w' }, { 0100, 'x' },
        { 0040, 'r' }, { 0020, 'w' }, { 0010, 'x' },
        { 0004, 'r' }, { 0002, 'w' }, { 0001, 'x' },
    };
    for (const auto &b : bits)
        out += (mode & b.bit) ? QLatin1Char(b.ch) : QLatin1Char('-');

    // setuid/setgid/sticky replace the x slot, ls-style.
    if (mode & 04000) out[3] = (mode & 0100) ? QLatin1Char('s') : QLatin1Char('S');
    if (mode & 02000) out[6] = (mode & 0010) ? QLatin1Char('s') : QLatin1Char('S');
    if (mode & 01000) out[9] = (mode & 0001) ? QLatin1Char('t') : QLatin1Char('T');
    return out;
}

const char *FileEntry::queryAttributes()
{
    return G_FILE_ATTRIBUTE_STANDARD_NAME ","
           G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
           G_FILE_ATTRIBUTE_STANDARD_TYPE ","
           G_FILE_ATTRIBUTE_STANDARD_SIZE ","
           G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
           G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP ","
           G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
           G_FILE_ATTRIBUTE_STANDARD_ICON ","
           G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
           G_FILE_ATTRIBUTE_TIME_MODIFIED ","
           G_FILE_ATTRIBUTE_TIME_CREATED ","
           G_FILE_ATTRIBUTE_TIME_CREATED_USEC ","
           G_FILE_ATTRIBUTE_TIME_ACCESS ","
           G_FILE_ATTRIBUTE_TIME_ACCESS_USEC ","
           G_FILE_ATTRIBUTE_OWNER_USER ","
           G_FILE_ATTRIBUTE_OWNER_GROUP ","
           G_FILE_ATTRIBUTE_UNIX_MODE ","
           G_FILE_ATTRIBUTE_TRASH_ORIG_PATH ","
           G_FILE_ATTRIBUTE_STANDARD_TARGET_URI;
}

FileEntry FileEntry::fromInfo(GFileInfo *info)
{
    FileEntry entry;

    // Generic accessors throughout: remote backends (gphoto2 in the field)
    // return infos missing attributes that were explicitly queried, and the
    // typed getters CRITICAL on an absent attribute rather than answering.
    if (const char *name = g_file_info_get_attribute_byte_string(info,
                                                                 G_FILE_ATTRIBUTE_STANDARD_NAME))
        entry.name = QString::fromUtf8(name);

    if (const char *display = g_file_info_get_attribute_string(info,
                                                               G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME))
        entry.displayName = QString::fromUtf8(display);
    else
        entry.displayName = entry.name;

    entry.isDir = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_STANDARD_TYPE)
        == G_FILE_TYPE_DIRECTORY;
    entry.isHidden = g_file_info_get_attribute_boolean(info,
                                                       G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN);
    entry.isBackup = g_file_info_get_attribute_boolean(info,
                                                       G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP);
    entry.isSymlink = g_file_info_get_attribute_boolean(info,
                                                        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK);
    entry.size = qint64(g_file_info_get_attribute_uint64(info,
                                                         G_FILE_ATTRIBUTE_STANDARD_SIZE));

    if (const char *type = g_file_info_get_attribute_string(info,
                                                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)) {
        entry.contentType = QString::fromUtf8(type);
        if (char *description = g_content_type_get_description(type)) {
            entry.typeDescription = QString::fromUtf8(description);
            g_free(description);
        }
    }

    // Generic accessor, not a typed getter: absent everywhere but trash:///,
    // and the typed getters log a CRITICAL for an absent attribute.
    if (const char *orig = g_file_info_get_attribute_byte_string(info,
                                                                 G_FILE_ATTRIBUTE_TRASH_ORIG_PATH))
        entry.origPath = QString::fromUtf8(orig);

    // recent:/// rows are pointers at a file elsewhere; thumbnails (and any
    // future delegate niceties) want the real location, as a plain path when
    // it is local.
    if (const char *target = g_file_info_get_attribute_string(info,
                                                              G_FILE_ATTRIBUTE_STANDARD_TARGET_URI)) {
        const QUrl url(QString::fromUtf8(target));
        entry.targetPath = url.isLocalFile() ? url.toLocalFile()
                                             : QString::fromUtf8(target);
    }

    if (GDateTime *modified = g_file_info_get_modification_date_time(info)) {
        entry.modified = QDateTime::fromSecsSinceEpoch(g_date_time_to_unix(modified));
        g_date_time_unref(modified);
    }

    // Millisecond precision (the *_USEC attributes are queried): files created
    // within the same second still sort deterministically by creation time.
    if (GDateTime *created = g_file_info_get_creation_date_time(info)) {
        entry.created = QDateTime::fromMSecsSinceEpoch(
            g_date_time_to_unix(created) * 1000 + g_date_time_get_microsecond(created) / 1000);
        g_date_time_unref(created);
    }

    if (GDateTime *accessed = g_file_info_get_access_date_time(info)) {
        entry.accessed = QDateTime::fromMSecsSinceEpoch(
            g_date_time_to_unix(accessed) * 1000 + g_date_time_get_microsecond(accessed) / 1000);
        g_date_time_unref(accessed);
    }

    // Generic accessors: absent on many backends (gvfs mounts have no unix
    // owner), and the generic getters return null/0 rather than logging.
    if (const char *owner = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_USER))
        entry.owner = QString::fromUtf8(owner);
    if (const char *group = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_GROUP))
        entry.group = QString::fromUtf8(group);
    entry.mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);

    // GIO hands back a themed icon carrying several names, most specific first
    // ("text-x-python", then "text-x-generic"). Keeping the whole list lets the
    // icon provider fall back through them against whatever theme is loaded.
    GIcon *icon = nullptr;
    if (GObject *obj = g_file_info_get_attribute_object(info, G_FILE_ATTRIBUTE_STANDARD_ICON))
        icon = G_ICON(obj);
    if (icon) {
        if (G_IS_THEMED_ICON(icon)) {
            const gchar *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            for (int i = 0; names && names[i]; ++i)
                entry.iconNames.append(QString::fromUtf8(names[i]));
        }
    }

    if (entry.iconNames.isEmpty())
        entry.iconNames.append(entry.isDir ? QStringLiteral("folder")
                                           : QStringLiteral("text-x-generic"));

    return entry;
}
