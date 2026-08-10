#include "DBusAdaptors.h"

#include "Application.h"

#include <QUrl>

namespace {

QStringList localPathsFromUris(const QStringList &uris)
{
    QStringList paths;
    paths.reserve(uris.size());
    for (const QString &uri : uris) {
        const QUrl url(uri);
        // Callers are inconsistent about whether they send a URI or a path;
        // both are accepted rather than silently doing nothing.
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
        else if (!url.scheme().isEmpty())
            continue; // remote URIs arrive with gvfs support in Phase 4
        else
            paths.append(uri);
    }
    return paths;
}

} // namespace

OmantaAdaptor::OmantaAdaptor(Application *application)
    : QDBusAbstractAdaptor(application)
    , m_application(application)
{
}

int OmantaAdaptor::windowCount() const
{
    return m_application->windowCount();
}

void OmantaAdaptor::OpenPaths(const QStringList &paths, bool newWindow)
{
    m_application->openPaths(paths, newWindow);
}

QVariantMap OmantaAdaptor::WindowState()
{
    return m_application->windowState();
}

void OmantaAdaptor::SelectPath(const QString &path)
{
    m_application->showItems({ path });
}

FileManager1Adaptor::FileManager1Adaptor(Application *application)
    : QDBusAbstractAdaptor(application)
    , m_application(application)
{
}

void FileManager1Adaptor::ShowFolders(const QStringList &uris, const QString &)
{
    m_application->openPaths(localPathsFromUris(uris), true);
}

void FileManager1Adaptor::ShowItems(const QStringList &uris, const QString &)
{
    m_application->showItems(localPathsFromUris(uris));
}

void FileManager1Adaptor::ShowItemProperties(const QStringList &uris, const QString &)
{
    m_application->showItemProperties(localPathsFromUris(uris));
}
