#pragma once

#include <QHash>
#include <QList>
#include <QVariantMap>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QQmlApplicationEngine;

// Owns the window set and the process lifetime.
//
// omanta is single-process/multi-window like Nautilus: launching it again
// does not start a second process, it asks the running one for another window.
// That is what makes `omanta --new-window` from a keybinding feel instant and
// what lets D-Bus callers reveal a file in an existing window.
class Application : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged)

public:
    explicit Application(QQmlApplicationEngine *engine, QObject *parent = nullptr);

    // Counts windows that actually still exist. Reporting the raw list size
    // would mean a dead window still counts — which is exactly how a lifetime
    // bug hid behind a green test once already.
    int windowCount() const;

    // Opens `path` in a new window, or in the current one when `newWindow` is
    // false and a window already exists.
    Q_INVOKABLE void openWindow(const QString &path, const QString &selectName = {});
    void openPaths(const QStringList &paths, bool newWindow);

    // Opens each item's parent folder and selects the item — FileManager1's
    // ShowItems, and what "reveal in file manager" means everywhere.
    void showItems(const QStringList &paths);

    // FileManager1's ShowItemProperties: reveal the items, then open the
    // properties dialog on them in the window that ended up showing them.
    void showItemProperties(const QStringList &paths);

    // The active window's observable state, for the D-Bus WindowState call.
    QVariantMap windowState() const;

    // Called by QML when a window closes, so the process can exit with the
    // last one. QML owns the window objects; this only tracks them.
    Q_INVOKABLE void windowClosed(QObject *window);

Q_SIGNALS:
    void windowCountChanged();

private:
    // Creates, tracks and raises a window, and hands it back — the caller
    // sometimes has something more to ask of the window it just opened.
    QObject *createWindow(const QString &path, const QString &selectName);

    // Groups paths by the folder they should be revealed in, returning the
    // parents in the order they were first seen.
    QStringList revealGrouped(const QStringList &paths,
                              QHash<QString, QStringList> *byParent) const;

    void raiseWindow(QObject *window);

    // The window currently displaying `path`, or nullptr. Used to raise an
    // existing window instead of opening a duplicate.
    QObject *windowShowing(const QString &path) const;

    QQmlApplicationEngine *m_engine;
    QList<QPointer<QObject>> m_windows;
};
