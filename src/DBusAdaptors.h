#pragma once

#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QVariantMap>

class Application;

// omanta' own interface, used only for single-instance handoff: a second
// launch forwards its argv here and exits rather than starting a process.
class OmantaAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.omarchy.omanta")

    // Exported so the window model can be asserted on from outside the
    // process — a multi-window app that cannot be inspected cannot be tested.
    Q_PROPERTY(int WindowCount READ windowCount)

public:
    explicit OmantaAdaptor(Application *application);

    int windowCount() const;

public Q_SLOTS:
    void OpenPaths(const QStringList &paths, bool newWindow);
    void SelectPath(const QString &path);

    // Read-only snapshot of the active window: path, selection, view mode,
    // tab count. Exists so the interface can be driven and checked from a
    // script rather than a screenshot.
    QVariantMap WindowState();

private:
    Application *m_application;
};

// The freedesktop standard every "Show in file manager" / "Open containing
// folder" button in the desktop ends up calling — browsers, Electron apps and
// xdg-desktop-portal's OpenDirectory all route here. Nautilus owns this name
// today; omanta claims it only when it is free, so both can be installed at
// once during testing.
class FileManager1Adaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1Adaptor(Application *application);

public Q_SLOTS:
    void ShowFolders(const QStringList &uris, const QString &startupId);
    void ShowItems(const QStringList &uris, const QString &startupId);
    void ShowItemProperties(const QStringList &uris, const QString &startupId);

private:
    Application *m_application;
};
