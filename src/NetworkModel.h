#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QtQmlIntegration>

#include <gio/gio.h>

#include "FileEntry.h"
#include "ServerStore.h"

// The listing behind network:/// — Nautilus parity for the Network view.
// Three sources, merged and deduplicated by URI:
//
//   1. Known servers from the ServerStore (addresses connected to before).
//      Deliberately never stat'ed: an unmounted server row must not cost a
//      network round-trip — or trigger a mount — just by being listed.
//   2. Currently mounted network shares from GVolumeMonitor ("connected").
//   3. Whatever gvfs discovers under network:/// (SMB browse, DNS-SD).
//
// Speaks DirectoryModel's roles, so the proxy and the views cannot tell the
// difference (the same trick StarredModel plays). The name role is the URI —
// unique, which keeps name-keyed selection correct. Activating a row
// navigates to the URI; the normal needsMount flow does the rest.
class NetworkModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    // The QML singleton instance, handed in by the Tab.
    Q_PROPERTY(ServerStore *store READ store WRITE setStore NOTIFY storeChanged)
    // Only an active model enumerates anything — every tab owns one of
    // these, and most tabs are never looking at network:///.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit NetworkModel(QObject *parent = nullptr);
    ~NetworkModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ServerStore *store() const { return m_store; }
    void setStore(ServerStore *store);
    bool active() const { return m_active; }
    void setActive(bool active);
    int count() const { return int(m_rows.size()); }

Q_SIGNALS:
    void storeChanged();
    void activeChanged();
    void countChanged();

private:
    struct Row {
        QString uri;
        FileEntry entry;
    };

    struct Pending {
        QPointer<NetworkModel> model;
        quint64 generation = 0;
    };

    static void onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onNextFiles(GObject *source, GAsyncResult *res, gpointer data);
    static void onMountsChanged(GVolumeMonitor *, GMount *, gpointer data);

    void reload();
    void applyRows();
    void appendUnique(QList<Row> &rows, Row row) const;

    QList<Row> immediateRows() const; // known servers + mounted shares
    static Row serverRow(const QString &uri);

    ServerStore *m_store = nullptr;
    bool m_active = false;
    QList<Row> m_rows;
    QList<Row> m_discovered;

    quint64 m_generation = 0;
    GCancellable *m_cancellable = nullptr;
    GVolumeMonitor *m_monitor = nullptr;
    gulong m_addedHandler = 0;
    gulong m_removedHandler = 0;
};
