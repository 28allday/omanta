#include "ServerStore.h"
#include "Location.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QUrl>

ServerStore::ServerStore(QObject *parent)
    : QObject(parent)
{
    load();

    // Watch for another process editing the file — same re-arm dance as the
    // starred and bookmarks watches.
    m_watcher = new QFileSystemWatcher(this);
    if (QFile::exists(filePath()))
        m_watcher->addPath(filePath());
    const QString dir = QFileInfo(filePath()).absolutePath();
    if (QDir(dir).exists())
        m_watcher->addPath(dir);
    auto rearm = [this] {
        if (m_saving)
            return; // our own write; state is already current
        load();
        if (QFile::exists(filePath()) && !m_watcher->files().contains(filePath()))
            m_watcher->addPath(filePath());
        Q_EMIT changed();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, rearm);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, rearm);
}

QString ServerStore::normalize(const QString &uri)
{
    QString out = uri.trimmed();
    while (out.endsWith(QLatin1Char('/')) && !out.endsWith(QStringLiteral("://")))
        out.chop(1);
    return out;
}

QString ServerStore::filePath() const
{
    const QString override = qEnvironmentVariable("OMANTA_SERVERS_FILE");
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + QStringLiteral("/.config/omanta/servers");
}

void ServerStore::load()
{
    m_uris.clear();
    m_order.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString uri = normalize(raw);
        if (uri.isEmpty() || m_uris.contains(uri))
            continue;
        m_uris.insert(uri);
        m_order.append(uri);
    }
}

void ServerStore::save()
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    m_saving = true;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        for (const QString &uri : std::as_const(m_order))
            file.write(uri.toUtf8() + '\n');
        file.close();
    }
    if (!m_watcher->files().contains(filePath()))
        m_watcher->addPath(filePath());
    // The watcher delivers our own write asynchronously; lift the guard once
    // that delivery has had its turn on the loop.
    QTimer::singleShot(200, this, [this] { m_saving = false; });
}

bool ServerStore::isKnown(const QString &uri) const
{
    return m_uris.contains(normalize(uri));
}

bool ServerStore::allKnown(const QStringList &uris) const
{
    if (uris.isEmpty())
        return false;
    for (const QString &uri : uris) {
        if (!isKnown(uri))
            return false;
    }
    return true;
}

void ServerStore::add(const QString &raw)
{
    const QString uri = normalize(raw);
    // A server is a remote URI by definition; a local path or file:// URI in
    // this list would just be a worse bookmark.
    if (uri.isEmpty() || !Location::isUri(uri) || Location::isLocal(uri)
        || !QUrl(uri).isValid() || m_uris.contains(uri))
        return;
    m_uris.insert(uri);
    m_order.append(uri);
    save();
    Q_EMIT changed();
}

void ServerStore::remove(const QStringList &uris)
{
    bool removed = false;
    for (const QString &raw : uris) {
        const QString uri = normalize(raw);
        if (m_uris.remove(uri)) {
            m_order.removeAll(uri);
            removed = true;
        }
    }
    if (removed) {
        save();
        Q_EMIT changed();
    }
}

QStringList ServerStore::uris() const
{
    return m_order;
}
