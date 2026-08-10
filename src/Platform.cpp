#include "Platform.h"
#include "ArchiveEngine.h"
#include "Location.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <gio/gio.h>

Platform::Platform(QObject *parent)
    : QObject(parent)
{
}

QString Platform::homePath() const
{
    return QDir::homePath();
}

QString Platform::parentPath(const QString &path) const
{
    return Location::parent(path);
}

QString Platform::baseName(const QString &path) const
{
    return Location::displayName(path);
}

bool Platform::isDir(const QString &path) const
{
    return QFileInfo(path).isDir();
}

bool Platform::exists(const QString &path) const
{
    return QFileInfo::exists(path);
}

bool Platform::isLocal(const QString &location) const
{
    return Location::isLocal(location);
}

bool Platform::isArchiveType(const QString &contentType) const
{
    return ArchiveEngine::isArchiveContentType(contentType);
}

bool Platform::activationExtracts(const QString &contentType) const
{
    if (!isArchiveType(contentType))
        return false;
    GAppInfo *info =
        g_app_info_get_default_for_type(contentType.toUtf8().constData(), FALSE);
    if (!info)
        return false;
    const char *id = g_app_info_get_id(info);
    const bool self = id && g_str_equal(id, "omanta.desktop");
    g_object_unref(info);
    return self;
}

QString Platform::archiveStem(const QString &name) const
{
    const int at = ArchiveEngine::archiveExtensionOffset(name);
    return at > 0 ? name.left(at) : name;
}

bool Platform::isNavigable(const QString &location) const
{
    if (Location::isUri(location))
        return true;
    return QFileInfo(location).isDir();
}

QStringList Platform::supportedSchemes() const
{
    QStringList schemes;
    const gchar *const *list = g_vfs_get_supported_uri_schemes(g_vfs_get_default());
    for (int i = 0; list && list[i]; ++i)
        schemes.append(QString::fromUtf8(list[i]));
    return schemes;
}

QVariantList Platform::templates() const
{
    QString dir = qEnvironmentVariable("OMANTA_TEMPLATES_DIR");
    if (dir.isEmpty()) {
        const char *xdg = g_get_user_special_dir(G_USER_DIRECTORY_TEMPLATES);
        dir = xdg ? QString::fromUtf8(xdg) : QDir::homePath() + QStringLiteral("/Templates");
    }
    // The XDG way of disabling a special dir is pointing it at $HOME.
    if (QDir::cleanPath(dir) == QDir::homePath())
        return {};

    QVariantList out;
    const auto entries = QDir(dir).entryInfoList(QDir::Files | QDir::Readable,
                                                QDir::Name | QDir::LocaleAware);
    for (const QFileInfo &info : entries) {
        if (info.fileName().startsWith(QLatin1Char('.')))
            continue;
        out.append(QVariantMap{ { QStringLiteral("name"), info.fileName() },
                                { QStringLiteral("path"), info.absoluteFilePath() } });
    }
    return out;
}

QString Platform::formatSize(qint64 bytes) const
{
    char *formatted = g_format_size(guint64(bytes));
    const QString result = QString::fromUtf8(formatted);
    g_free(formatted);
    return result;
}

QString Platform::formatItemCount(int count) const
{
    // Folders' "size": Nautilus's item count, em dash while unknown (not yet
    // counted, counting off, or the folder unreadable).
    if (count < 0)
        return QStringLiteral("—");
    if (count == 1)
        return QStringLiteral("1 item");
    return QStringLiteral("%L1 items").arg(count);
}

QString Platform::formatModified(const QDateTime &when, const QString &format) const
{
    if (!when.isValid())
        return {};

    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale;

    // Detailed is Nautilus's other preference value: the full numeric date
    // and time every time, nothing relative.
    if (format == QLatin1String("detailed"))
        return locale.toString(when, QLocale::ShortFormat);

    // Simple, Nautilus-style: today gets a time, the last week is relative,
    // this year gets a day and month, anything older gets the year too.
    if (when.date() == now.date())
        return QStringLiteral("Today, %1").arg(locale.toString(when.time(), QLocale::ShortFormat));
    const qint64 daysAgo = when.date().daysTo(now.date());
    if (daysAgo == 1)
        return QStringLiteral("Yesterday");
    if (daysAgo > 1 && daysAgo < 7)
        return QStringLiteral("%1 days ago").arg(daysAgo);
    if (when.date().year() == now.date().year())
        return locale.toString(when.date(), QStringLiteral("d MMM"));
    return locale.toString(when.date(), QStringLiteral("d MMM yyyy"));
}

QString Platform::formatTimestamp(const QDateTime &when) const
{
    if (!when.isValid())
        return {};

    // Deliberately not QLocale::LongFormat on the whole QDateTime: that appends
    // the timezone's full name ("British Summer Time"), which is three words
    // answering a question nobody looking at a local file has asked.
    const QLocale locale;
    return locale.toString(when.date(), QLocale::LongFormat)
           + QLatin1String(" at ")
           + locale.toString(when.time(), QLocale::ShortFormat);
}

bool Platform::openPath(const QString &path) const
{
    const QByteArray uri = Location::isUri(path)
        ? path.toUtf8()
        : QUrl::fromLocalFile(path).toString().toUtf8();

    GError *error = nullptr;
    const bool ok = g_app_info_launch_default_for_uri(uri.constData(), nullptr, &error);
    if (!ok) {
        qWarning("omanta: cannot open %s: %s", qUtf8Printable(path),
                 error ? error->message : "no handler");
    }
    g_clear_error(&error);
    return ok;
}

bool Platform::openTerminal(const QString &directory) const
{
    if (!Location::isLocal(directory))
        return false;

    // Ordered by how specific the user's intent is: an explicit $TERMINAL wins,
    // then the freedesktop-blessed launcher, then the Debian-style alternative.
    // Nothing here names a particular terminal emulator.
    QStringList candidates;
    if (const QString configured = qEnvironmentVariable("TERMINAL"); !configured.isEmpty())
        candidates << configured;
    candidates << QStringLiteral("xdg-terminal-exec")
               << QStringLiteral("x-terminal-emulator");

    for (const QString &candidate : std::as_const(candidates)) {
        const QString program = QStandardPaths::findExecutable(candidate);
        if (program.isEmpty())
            continue;
        if (QProcess::startDetached(program, {}, directory))
            return true;
    }

    qWarning("omanta: no terminal found (tried $TERMINAL, xdg-terminal-exec, x-terminal-emulator)");
    return false;
}

QString Platform::resolvePath(const QString &input, const QString &base) const
{
    QString path = input.trimmed();
    if (path.isEmpty())
        return {};

    // A typed URI names the place itself; nothing to resolve against.
    if (Location::isUri(path))
        return Location::clean(path);

    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);

    if (!path.startsWith(QLatin1Char('/')) && !base.isEmpty())
        path = QDir(base).filePath(path);

    return QDir::cleanPath(path);
}

QString Platform::uriList(const QStringList &locations) const
{
    // text/uri-list is CRLF-separated URIs (RFC 2483). GFile does the
    // escaping; local paths become file:// URIs.
    QStringList uris;
    for (const QString &location : locations) {
        GFile *file = Location::make(location);
        char *uri = g_file_get_uri(file);
        uris.append(QString::fromUtf8(uri));
        g_free(uri);
        g_object_unref(file);
    }
    return uris.join(QStringLiteral("\r\n"));
}

QStringList Platform::locationsFromUrls(const QVariantList &urls) const
{
    QStringList locations;
    for (const QVariant &entry : urls) {
        const QString location = Location::clean(entry.toUrl().toString());
        if (!location.isEmpty())
            locations.append(location);
    }
    return locations;
}

bool Platform::sameFilesystem(const QString &a, const QString &b) const
{
    const auto filesystemId = [](const QString &location) -> QString {
        GFile *file = Location::make(location);
        GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                            G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
        g_object_unref(file);
        if (!info)
            return {};
        QString id;
        if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ID_FILESYSTEM))
            id = QString::fromUtf8(
                g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_ID_FILESYSTEM));
        g_object_unref(info);
        return id;
    };

    const QString idA = filesystemId(a);
    // An unanswerable question (either side unreachable) is treated as
    // "different", which errs toward copy — the non-destructive default.
    return !idA.isEmpty() && idA == filesystemId(b);
}

int Platform::keyboardModifiers() const
{
    return int(QGuiApplication::queryKeyboardModifiers());
}

QStringList Platform::collisions(const QStringList &paths, const QString &destinationDir) const
{
    // Through GIO rather than QDir so the destination may be a URI. For a
    // remote destination the existence checks are synchronous network calls,
    // which is accepted: this runs once, on an explicit paste.
    QStringList clashing;
    GFile *destination = Location::make(destinationDir);

    for (const QString &path : paths) {
        GFile *source = Location::make(path);
        char *name = g_file_get_basename(source);
        GFile *target = g_file_get_child(destination, name);

        // Pasting into the folder a file already lives in is not a conflict —
        // it is how you duplicate something. Asking "replace or keep both?"
        // when the only clash is the file with itself is a pointless question,
        // and the engine already turns it into "name (copy)".
        if (g_file_query_exists(target, nullptr) && !g_file_equal(target, source))
            clashing.append(QString::fromUtf8(name));

        g_object_unref(target);
        g_free(name);
        g_object_unref(source);
    }

    g_object_unref(destination);
    return clashing;
}

QVariantList Platform::pathCrumbs(const QString &path) const
{
    QVariantList crumbs;
    if (path.isEmpty())
        return crumbs;

    // A URI breadcrumb starts at the root of its scheme — "Trash", or the
    // host of a mount — and walks the decoded segments below it.
    if (Location::isUri(path)) {
        QUrl url(path);
        QUrl walked(url);
        walked.setPath(QStringLiteral("/"));
        crumbs.append(QVariantMap{
            { QStringLiteral("label"), Location::displayName(walked.toString()) },
            { QStringLiteral("path"), Location::clean(walked.toString()) } });

        const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString soFar;
        for (const QString &part : parts) {
            soFar += QLatin1Char('/') + part;
            walked.setPath(soFar);
            crumbs.append(QVariantMap{ { QStringLiteral("label"), part },
                                       { QStringLiteral("path"), Location::clean(walked.toString()) } });
        }
        return crumbs;
    }

    const QString home = QDir::homePath();
    QString walked;

    // Paths under $HOME start at "Home" rather than repeating /home/<user>,
    // which is what keeps the breadcrumb readable in the common case.
    QString remainder = path;
    if (path == home || path.startsWith(home + QLatin1Char('/'))) {
        crumbs.append(QVariantMap{ { QStringLiteral("label"), QStringLiteral("Home") },
                                   { QStringLiteral("path"), home } });
        walked = home;
        remainder = path.mid(home.length());
    } else {
        crumbs.append(QVariantMap{ { QStringLiteral("label"), QStringLiteral("/") },
                                   { QStringLiteral("path"), QStringLiteral("/") } });
        walked.clear();
    }

    const QStringList parts = remainder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        walked += QLatin1Char('/') + part;
        crumbs.append(QVariantMap{ { QStringLiteral("label"), part },
                                   { QStringLiteral("path"), walked } });
    }

    return crumbs;
}
