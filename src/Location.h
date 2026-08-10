#pragma once

#include <QString>

#include <gio/gio.h>

// A location string is either an absolute local path ("/home/user/Pictures") or
// a URI ("trash:///", "smb://server/share"). Local files stay plain paths —
// every existing caller, test and D-Bus message keeps meaning what it meant —
// and URIs only appear once the sidebar introduces places that have no path.
//
// These functions are the one place that distinction lives. Nothing else in
// the codebase may ask "does it start with a slash".
namespace Location {

// True for URI-shaped strings, including file:// (which clean() turns back
// into a plain path — after cleaning, a local location never looks like one).
bool isUri(const QString &location);

// True when the location is on the local filesystem — the gate for things
// that genuinely need a path: terminals, thumbnails, sync stat calls.
bool isLocal(const QString &location);

// Canonical form: cleaned absolute path for anything local (file:// included),
// GIO's normalized URI for the rest. Empty stays empty.
QString clean(const QString &location);

// GFile for a location. Caller owns the reference.
GFile *make(const QString &location);

// The canonical location string for a GFile: path when it has one, URI when
// it does not.
QString fromGFile(GFile *file);

// Child of a location by display name, correctly escaped for URIs.
QString child(const QString &location, const QString &name);

// Like child(), but `relPath` may span several segments ("a/b/c.txt") —
// what a recursive search accumulates.
QString descend(const QString &location, const QString &relPath);

// Parent location, or empty at a root — the top of a mount as much as "/".
QString parent(const QString &location);

// What a tab or crumb should call this location: "Trash" for trash:///, the
// host for a mount root, the decoded last segment otherwise.
QString displayName(const QString &location);

} // namespace Location
