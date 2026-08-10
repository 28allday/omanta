#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtQmlIntegration>

class QFileSystemWatcher;

// Starred files — Nautilus parity for the sidebar's Starred entry.
//
// Nautilus keeps its stars in the tracker3 database; omanta keeps a plain
// file (~/.config/omanta/starred, one absolute path per line) because a
// SPARQL dependency for a list of paths is absurd. The UX matches — star and
// unstar from the context menu, a Starred place that lists them — but stars
// do not carry over between the two file managers, and that is accepted.
class StarredStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit StarredStore(QObject *parent = nullptr);

    Q_INVOKABLE bool isStarred(const QString &path) const;
    // True only when the whole selection is starred — what decides whether
    // the menu item reads "Star" or "Unstar".
    Q_INVOKABLE bool allStarred(const QStringList &paths) const;
    Q_INVOKABLE void star(const QStringList &paths);
    Q_INVOKABLE void unstar(const QStringList &paths);

    QStringList paths() const;

Q_SIGNALS:
    void changed();

private:
    QString filePath() const;
    void load();
    void save();

    QSet<QString> m_paths;
    QStringList m_order; // insertion order, so the Starred view is stable
    QFileSystemWatcher *m_watcher = nullptr;
    bool m_saving = false;
};
