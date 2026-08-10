#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QtQmlIntegration>

#include <gio/gio.h>

#include "FileEntry.h"
#include "StarredStore.h"

// The listing behind starred:/// — the store's paths, stat'ed through GIO so
// every row carries the same attributes as a directory listing. Speaks
// DirectoryModel's roles, so the proxy and the views cannot tell the
// difference (the same trick SearchModel plays).
//
// The name role is the full path: unique across folders, which keeps
// name-keyed selection and type-ahead correct when two starred files share a
// basename. displayName stays the basename.
class StarredModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    // The QML singleton instance, handed in by the Tab.
    Q_PROPERTY(StarredStore *store READ store WRITE setStore NOTIFY storeChanged)
    // Only an active model stats anything — every tab owns one of these, and
    // most tabs are never looking at starred:///.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit StarredModel(QObject *parent = nullptr);
    ~StarredModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    StarredStore *store() const { return m_store; }
    void setStore(StarredStore *store);
    bool active() const { return m_active; }
    void setActive(bool active);
    int count() const { return int(m_rows.size()); }

Q_SIGNALS:
    void storeChanged();
    void activeChanged();
    void countChanged();

private:
    struct Row {
        QString path;
        FileEntry entry;
    };

    struct Pending {
        QPointer<StarredModel> model;
        quint64 generation = 0;
        int index = 0;
    };

    static void onInfoReady(GObject *source, GAsyncResult *res, gpointer data);

    void reload();
    void finishLoad();

    StarredStore *m_store = nullptr;
    bool m_active = false;
    QList<Row> m_rows;

    quint64 m_generation = 0;
    int m_outstanding = 0;
    QList<Row> m_loading; // slot per starred path; missing files stay empty
    GCancellable *m_cancellable = nullptr;
};
