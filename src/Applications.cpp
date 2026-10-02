#include "Applications.h"
#include "Location.h"

#include <QVariantMap>

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

        result.append(QVariantMap{
            { QStringLiteral("id"), QString::fromUtf8(id) },
            { QStringLiteral("name"), QString::fromUtf8(g_app_info_get_display_name(application)) },
            { QStringLiteral("iconSource"),
              QStringLiteral("image://fileicon/") + iconNamesOf(application).join(QLatin1Char(',')) },
            { QStringLiteral("isDefault"), QString::fromUtf8(id) == defaultId },
        });
    }
    g_list_free_full(all, g_object_unref);

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

bool launch(const QString &applicationId, const QString &contentType,
            const QStringList &paths)
{
    if (paths.isEmpty())
        return false;

    GAppInfo *application = find(applicationId, contentType);
    if (!application)
        return false;

    GList *files = nullptr;
    for (const QString &path : paths)
        files = g_list_append(files, Location::make(path));

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

} // namespace Applications
