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

// The applications the app launcher shows, by display name. Desktop entries
// only: not every executable, and not the entries the launcher hides.
// forType() is narrower — only handlers registered for one type — which is
// why a launcher app is missing until the user adds it from this list.
QVariantList all();

// The GAppInfo behind a desktop-file id, looked up in the list forType() is
// built from — a desktop id can name an entry that GDesktopAppInfo alone will
// not load. Null when the id is not registered for the type. Caller owns the
// reference.
GAppInfo *find(const QString &applicationId, const QString &contentType);

// Any installed application with this desktop-file id, whether or not a type
// claims it. Null for an empty id, a path, or an id nothing installed owns.
// Caller owns the reference.
GAppInfo *findInstalled(const QString &applicationId);

// Launches `paths` with an application the caller already holds. Does not
// unref it. False when there is nothing to open or the launch fails.
bool launch(GAppInfo *application, const QStringList &paths);

// Launches `paths` with an application registered for `contentType`. False if
// the id is not one of them or the launch fails.
bool launch(const QString &applicationId, const QString &contentType,
            const QStringList &paths);

// Launches `paths` with any installed application, registered for the file's
// type or not. The context menu does not use this: only an app the user
// picked out of all() should be started this way.
bool launchInstalled(const QString &applicationId, const QStringList &paths);

} // namespace Applications
