#include "Location.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace Location {

namespace {

// The roots GIO gives friendly virtual filesystems. Keys are schemes.
QString rootLabel(const QString &scheme)
{
    if (scheme == QLatin1String("trash"))
        return QStringLiteral("Trash");
    if (scheme == QLatin1String("starred"))
        return QStringLiteral("Starred");
    if (scheme == QLatin1String("recent"))
        return QStringLiteral("Recent");
    if (scheme == QLatin1String("network"))
        return QStringLiteral("Network");
    if (scheme == QLatin1String("computer"))
        return QStringLiteral("Computer");
    return {};
}

} // namespace

bool isUri(const QString &location)
{
    // A scheme is letters/digits/+-. followed by "://". A Unix path cannot
    // contain "://" before its first slash, so this cannot misfire on one.
    const qsizetype mark = location.indexOf(QLatin1String("://"));
    if (mark <= 0)
        return false;
    for (qsizetype i = 0; i < mark; ++i) {
        const QChar c = location.at(i);
        if (!c.isLetterOrNumber() && c != QLatin1Char('+') && c != QLatin1Char('-')
            && c != QLatin1Char('.'))
            return false;
    }
    return true;
}

bool isLocal(const QString &location)
{
    if (!isUri(location))
        return true;
    return QUrl(location).isLocalFile();
}

QString clean(const QString &location)
{
    if (location.isEmpty())
        return {};

    if (!isUri(location))
        return QDir::cleanPath(location);

    const QUrl url(location);
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());

    // Round-tripping through GIO is the URI normalizer: it settles trailing
    // slashes and escaping so "the same place" is also the same string —
    // which is what tab titles, history and the model's change check compare.
    GFile *file = g_file_new_for_uri(location.toUtf8().constData());
    const QString normalized = fromGFile(file);
    g_object_unref(file);
    return normalized;
}

GFile *make(const QString &location)
{
    if (isUri(location))
        return g_file_new_for_uri(location.toUtf8().constData());
    return g_file_new_for_path(location.toUtf8().constData());
}

QString fromGFile(GFile *file)
{
    if (!file)
        return {};
    // Native files only: a mounted gvfs location also has a path — the fuse
    // one — and taking it would flip the location bar from "gphoto2://…" to
    // /run/user/…/gvfs the moment the mount appears. Nautilus keeps the URI.
    if (g_file_is_native(file)) {
        if (char *path = g_file_get_path(file)) {
            const QString result = QString::fromUtf8(path);
            g_free(path);
            return result;
        }
    }
    char *uri = g_file_get_uri(file);
    const QString result = QString::fromUtf8(uri);
    g_free(uri);
    return result;
}

QString child(const QString &location, const QString &name)
{
    if (!isUri(location))
        return QDir(location).filePath(name);

    GFile *dir = make(location);
    GFile *entry = g_file_get_child(dir, name.toUtf8().constData());
    const QString result = fromGFile(entry);
    g_object_unref(entry);
    g_object_unref(dir);
    return result;
}

QString descend(const QString &location, const QString &relPath)
{
    if (!isUri(location))
        return QDir(location).filePath(relPath);

    GFile *dir = make(location);
    GFile *entry = g_file_resolve_relative_path(dir, relPath.toUtf8().constData());
    const QString result = fromGFile(entry);
    g_object_unref(entry);
    g_object_unref(dir);
    return result;
}

QString parent(const QString &location)
{
    if (!isUri(location)) {
        if (location.isEmpty() || location == QLatin1String("/"))
            return {};
        QDir dir(location);
        if (!dir.cdUp())
            return {};
        return dir.absolutePath();
    }

    // Syntactic, not through g_file_get_parent: for an unmounted URI GIO's
    // answer depends on which gvfs client modules happen to be loaded, and a
    // parent that exists on the desktop but not in a test is a trap.
    QUrl url(location);
    QString path = url.path();
    while (path.length() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.isEmpty() || path == QLatin1String("/"))
        return {}; // the top of the scheme or mount — Up ends here, like "/"
    const qsizetype cut = path.lastIndexOf(QLatin1Char('/'));
    url.setPath(cut <= 0 ? QStringLiteral("/") : path.left(cut));
    return clean(url.toString());
}

QString displayName(const QString &location)
{
    if (!isUri(location)) {
        if (location == QLatin1String("/"))
            return location;
        return QFileInfo(location).fileName();
    }

    const QUrl url(location);
    const QString path = url.path();

    if (path.isEmpty() || path == QLatin1String("/")) {
        const QString label = rootLabel(url.scheme());
        if (!label.isEmpty())
            return label;
        if (!url.host().isEmpty())
            return url.host();
        return url.scheme();
    }

    const QString name = path.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    return name.isEmpty() ? path : name;
}

} // namespace Location
