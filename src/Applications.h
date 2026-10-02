#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <gio/gio.h>

// The applications registered for a content type, as both Open With surfaces
// see them: the context menu's submenu (Platform) and the Properties dialog's
// tab (FileProperties). One lookup rule, so the two can never disagree about
// what is offered or what a chosen id launches.
namespace Applications {

// Each entry a map of id/name/iconSource/isDefault, in GIO's order. Empty for
// an empty type or one nothing is registered for.
QVariantList forType(const QString &contentType);

// The GAppInfo behind a desktop-file id, looked up in the list forType() is
// built from — a desktop id can name an entry that GDesktopAppInfo alone will
// not load. Null when the id is not registered for the type. Caller owns the
// reference.
GAppInfo *find(const QString &applicationId, const QString &contentType);

// Launches `paths` with an application registered for `contentType`. False if
// the id is not one of them or the launch fails.
bool launch(const QString &applicationId, const QString &contentType,
            const QStringList &paths);

} // namespace Applications
