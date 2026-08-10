#include "StarredStore.h"
#include "Location.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

StarredStore::StarredStore(QObject *parent)
    : QObject(parent)
{
    load();

    // Watch for another process editing the file. Within one process the
    // singleton is already shared, so this is about a second omanta (or a
    // hand edit) — same re-arm dance as the bookmarks watch.
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

QString StarredStore::filePath() const
{
    const QString override = qEnvironmentVariable("OMANTA_STARRED_FILE");
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + QStringLiteral("/.config/omanta/starred");
}

void StarredStore::load()
{
    m_paths.clear();
    m_order.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString path = raw.trimmed();
        if (path.isEmpty() || m_paths.contains(path))
            continue;
        m_paths.insert(path);
        m_order.append(path);
    }
}

void StarredStore::save()
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    m_saving = true;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        for (const QString &path : std::as_const(m_order))
            file.write(path.toUtf8() + '\n');
        file.close();
    }
    if (!m_watcher->files().contains(filePath()))
        m_watcher->addPath(filePath());
    // The watcher delivers our own write asynchronously; lift the guard once
    // that delivery has had its turn on the loop.
    QTimer::singleShot(200, this, [this] { m_saving = false; });
}

bool StarredStore::isStarred(const QString &path) const
{
    return m_paths.contains(Location::clean(path));
}

bool StarredStore::allStarred(const QStringList &paths) const
{
    if (paths.isEmpty())
        return false;
    for (const QString &path : paths) {
        if (!isStarred(path))
            return false;
    }
    return true;
}

void StarredStore::star(const QStringList &paths)
{
    bool added = false;
    for (const QString &raw : paths) {
        const QString path = Location::clean(raw);
        // Stars are for real files; a trash:// row has nothing to pin.
        if (path.isEmpty() || !Location::isLocal(path) || m_paths.contains(path))
            continue;
        m_paths.insert(path);
        m_order.append(path);
        added = true;
    }
    if (added) {
        save();
        Q_EMIT changed();
    }
}

void StarredStore::unstar(const QStringList &paths)
{
    bool removed = false;
    for (const QString &raw : paths) {
        const QString path = Location::clean(raw);
        if (m_paths.remove(path)) {
            m_order.removeAll(path);
            removed = true;
        }
    }
    if (removed) {
        save();
        Q_EMIT changed();
    }
}

QStringList StarredStore::paths() const
{
    return m_order;
}
