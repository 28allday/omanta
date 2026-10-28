#include "Applications.h"
#include "Location.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QVariantMap>

#include <gio/gdesktopappinfo.h>

#include <algorithm>

namespace {

QStringList iconNamesOf(GAppInfo *application)
{
    QStringList names;
    if (GIcon *icon = g_app_info_get_icon(application)) {
        if (G_IS_THEMED_ICON(icon)) {
            const gchar *const *iconNames = g_themed_icon_get_names(G_THEMED_ICON(icon));
            for (int i = 0; iconNames && iconNames[i]; ++i)
                names.append(QString::fromUtf8(iconNames[i]));
        }
    }
    if (names.isEmpty())
        names.append(QStringLiteral("application-x-executable"));
    return names;
}

// The Omarchy app launcher's own hide list. These desktop entries are real
// applications, and GIO would show them, but the launcher takes them out
// (electron runtimes, jconsole, btop, and the rest). The chooser follows
// that list so it offers what the launcher offers, not every executable.
QSet<QString> launcherHiddenIds()
{
    QSet<QString> hidden;
    const QString root = qEnvironmentVariable("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
    QFile file(root + QStringLiteral("/default/omarchy/launcher.hides"));
    if (!file.open(QIODevice::ReadOnly))
        return hidden;

    while (!file.atEnd()) {
        const QString id = QString::fromUtf8(file.readLine()).trimmed();
        if (id.isEmpty() || id.startsWith(QLatin1Char('#')))
            continue;
        hidden.insert(id.endsWith(QLatin1String(".desktop")) ? id.chopped(8) : id);
    }
    return hidden;
}

QString launcherIdOf(const QString &desktopId)
{
    return desktopId.endsWith(QLatin1String(".desktop")) ? desktopId.chopped(8) : desktopId;
}

QVariantMap entryFor(GAppInfo *application, const QString &defaultId)
{
    const char *id = g_app_info_get_id(application);
    const QString applicationId = QString::fromUtf8(id ? id : "");
    const char *display = g_app_info_get_display_name(application);
    return {
        { QStringLiteral("id"), applicationId },
        { QStringLiteral("name"), QString::fromUtf8(display ? display : "") },
        { QStringLiteral("iconSource"),
          QStringLiteral("image://fileicon/") + iconNamesOf(application).join(QLatin1Char(',')) },
        { QStringLiteral("isDefault"), !defaultId.isEmpty() && applicationId == defaultId },
    };
}

// What the chooser's search matches besides the name: the generic name
// ("Image Editor"), the entry's keywords, and the program it runs. Someone
// looking for an app often knows what it does, or its command, rather than
// the name its desktop entry gives it.
QString searchTextOf(GAppInfo *application)
{
    QStringList words;
    if (G_IS_DESKTOP_APP_INFO(application)) {
        auto *desktop = G_DESKTOP_APP_INFO(application);
        if (const char *generic = g_desktop_app_info_get_generic_name(desktop))
            words.append(QString::fromUtf8(generic));
        if (const char *const *keywords = g_desktop_app_info_get_keywords(desktop)) {
            for (const char *const *keyword = keywords; *keyword; ++keyword)
                words.append(QString::fromUtf8(*keyword));
        }
    }
    if (const char *executable = g_app_info_get_executable(application))
        words.append(QFileInfo(QString::fromUtf8(executable)).fileName());
    return words.join(QLatin1Char(' '));
}

} // namespace

namespace Applications {

QVariantList forType(const QString &contentType)
{
    QVariantList result;
    if (contentType.isEmpty())
        return result;

    const QByteArray type = contentType.toUtf8();

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
        result.append(entryFor(application, defaultId));
    }
    g_list_free_full(all, g_object_unref);

    return result;
}

QVariantList all()
{
    QVariantList result;
    QStringList seen;
    const QSet<QString> hidden = launcherHiddenIds();

    // Desktop applications only, the same set the app launcher draws from.
    // A program that is merely a file in /usr/bin has no desktop entry and
    // never appears here.
    GList *installed = g_app_info_get_all();
    for (GList *item = installed; item; item = item->next) {
        auto *application = static_cast<GAppInfo *>(item->data);
        // NoDisplay, Hidden, and OnlyShowIn/NotShowIn — the launcher drops
        // these too. should_show is that check.
        if (!g_app_info_should_show(application))
            continue;

        const char *id = g_app_info_get_id(application);
        const char *display = g_app_info_get_display_name(application);
        if (!id || !id[0] || !display || !display[0])
            continue;
        const QString applicationId = QString::fromUtf8(id);
        if (hidden.contains(launcherIdOf(applicationId)) || seen.contains(applicationId))
            continue;
        seen.append(applicationId);
        QVariantMap entry = entryFor(application, QString());
        entry.insert(QStringLiteral("search"), searchTextOf(application));
        result.append(entry);
    }
    g_list_free_full(installed, g_object_unref);

    std::sort(result.begin(), result.end(), [](const QVariant &left, const QVariant &right) {
        return QString::localeAwareCompare(left.toMap().value(QStringLiteral("name")).toString(),
                                            right.toMap().value(QStringLiteral("name")).toString())
               < 0;
    });
    return result;
}

GAppInfo *find(const QString &applicationId, const QString &contentType)
{
    if (applicationId.isEmpty() || contentType.isEmpty())
        return nullptr;

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

GAppInfo *findInstalled(const QString &applicationId)
{
    // A desktop id is "name.desktop", never a path. Rejecting a slash keeps a
    // crafted id from loading a desktop file the user did not install.
    // Looked up in the same list all() is built from: a desktop id can name
    // an entry that GDesktopAppInfo alone will not load.
    if (applicationId.isEmpty() || applicationId.contains(QLatin1Char('/')))
        return nullptr;

    const QByteArray wanted = applicationId.toUtf8();
    GList *installed = g_app_info_get_all();
    GAppInfo *found = nullptr;
    for (GList *item = installed; item; item = item->next) {
        auto *application = static_cast<GAppInfo *>(item->data);
        const char *id = g_app_info_get_id(application);
        if (id && wanted == id) {
            found = static_cast<GAppInfo *>(g_object_ref(application));
            break;
        }
    }
    g_list_free_full(installed, g_object_unref);
    return found;
}

bool launch(GAppInfo *application, const QStringList &paths)
{
    if (!application || paths.isEmpty())
        return false;

    const char *id = g_app_info_get_id(application);
    GList *files = nullptr;
    for (const QString &path : paths)
        files = g_list_append(files, Location::make(path));

    GError *error = nullptr;
    const bool ok = g_app_info_launch(application, files, nullptr, &error);
    if (!ok) {
        qWarning("omanta: could not launch %s: %s", id ? id : "(unknown)",
                 error ? error->message : "unknown");
    }
    g_clear_error(&error);

    g_list_free_full(files, g_object_unref);
    return ok;
}

bool launch(const QString &applicationId, const QString &contentType,
            const QStringList &paths)
{
    GAppInfo *application = find(applicationId, contentType);
    if (!application)
        return false;

    const bool ok = launch(application, paths);
    g_object_unref(application);
    return ok;
}

bool launchInstalled(const QString &applicationId, const QStringList &paths)
{
    GAppInfo *application = findInstalled(applicationId);
    if (!application)
        return false;

    const bool ok = launch(application, paths);
    g_object_unref(application);
    return ok;
}

} // namespace Applications
