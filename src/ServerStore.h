#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QtQmlIntegration>

class QFileSystemWatcher;

// Known network servers — Nautilus parity for the Network view's connection
// list. Every address successfully connected to through the server bar is
// remembered here and offered again next time.
//
// Nautilus keeps its list in an XBEL file (~/.config/nautilus/servers);
// omanta keeps a plain file (~/.config/omanta/servers, one URI per line)
// for the same reason the starred store does — the format is a list of
// strings, so the file is one. Connections do not carry over between the two
// file managers, and that is accepted.
class ServerStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ServerStore(QObject *parent = nullptr);

    Q_INVOKABLE bool isKnown(const QString &uri) const;
    // True only when the whole selection is remembered servers — what gates
    // the context menu's Forget Connection.
    Q_INVOKABLE bool allKnown(const QStringList &uris) const;
    Q_INVOKABLE void add(const QString &uri);
    Q_INVOKABLE void remove(const QStringList &uris);

    QStringList uris() const;

Q_SIGNALS:
    void changed();

private:
    // Canonical form for storage and lookup: no trailing slash, so the URI a
    // user typed and the URI a mount reports cannot disagree about one.
    static QString normalize(const QString &uri);

    QString filePath() const;
    void load();
    void save();

    QSet<QString> m_uris;
    QStringList m_order; // insertion order, so the listing is stable
    QFileSystemWatcher *m_watcher = nullptr;
    bool m_saving = false;
};
