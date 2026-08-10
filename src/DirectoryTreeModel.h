#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QtQmlIntegration>

#include "DirectoryModel.h"
#include "FileSortFilterModel.h"

// Expandable folders for the list view (Nautilus's use-tree-view): a flat
// projection of a tree, because the views are ListViews and everything the
// tab knows — selection, type-ahead, rubber-band arithmetic — speaks flat
// rows.
//
// Shape: one DirectoryModel + FileSortFilterModel pair per expanded folder
// (the root pair is the tab's own dirModel, borrowed), flattened depth-first
// so children follow their parent. Each expanded folder keeps its own live
// monitor, sorting and hidden-filtering for free — the machinery is exactly
// the flat view's, just instantiated per node.
//
// Contracts worth knowing before editing:
//   - The name role is the path RELATIVE to the tab root ("zebra/nested.txt");
//     depth-0 rows keep their plain name. Selection is keyed by name, so tree
//     selections stay unique and top-level ones survive a view-mode switch.
//   - Live changes never reset the model. Every structural change from a node
//     is a single contiguous run of rows, found by diffing the fresh walk
//     against the old one on stable identity (node, relative path) — the view
//     keeps its scroll position. Reorders travel as layoutChanged; only a
//     mixed change nothing else can express falls back to a reset.
//   - It mirrors the slice of FileSortFilterModel's surface the QML layer
//     uses (count, valueAt, proxyRowForName, findByPrefix, the sort/hidden
//     properties), so Tab.files can be either without the views knowing.
class DirectoryTreeModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    // The tab's own directory model — borrowed, never owned. Its path is the
    // tree's root; navigating it collapses everything.
    Q_PROPERTY(DirectoryModel *rootModel READ rootModel WRITE setRootModel NOTIFY rootModelChanged)
    Q_PROPERTY(int sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortDescendingChanged)
    Q_PROPERTY(bool foldersFirst READ foldersFirst WRITE setFoldersFirst NOTIFY foldersFirstChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit DirectoryTreeModel(QObject *parent = nullptr);
    ~DirectoryTreeModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    DirectoryModel *rootModel() const { return m_rootModel; }
    void setRootModel(DirectoryModel *model);

    int sortKey() const { return m_sortKey; }
    void setSortKey(int key);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    bool foldersFirst() const { return m_foldersFirst; }
    void setFoldersFirst(bool foldersFirst);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool showHidden);
    int count() const { return int(m_flat.size()); }

    Q_INVOKABLE void expand(int row);
    Q_INVOKABLE void collapse(int row);
    Q_INVOKABLE void toggleExpanded(int row);

    // The FileSortFilterModel surface Tab and the views actually call.
    Q_INVOKABLE QVariant valueAt(int row, const QString &roleName) const;
    Q_INVOKABLE int proxyRowForName(const QString &name) const;
    Q_INVOKABLE int findByPrefix(const QString &prefix, int startRow) const;

Q_SIGNALS:
    void rootModelChanged();
    void sortKeyChanged();
    void sortDescendingChanged();
    void foldersFirstChanged();
    void showHiddenChanged();
    void countChanged();

private:
    // One expanded folder. The root node borrows the tab's model; every other
    // node owns its own, so collapsing releases its monitor with it.
    struct TreeNode {
        QString relPath; // "" for the root
        int depth = 0;   // of the node's rows: segments in relPath
        DirectoryModel *model = nullptr;
        FileSortFilterModel *proxy = nullptr;
        bool ownsModel = false;
    };

    // One visible row. rel is the stable identity a diff can trust: proxy row
    // numbers shift on every insert above, relative paths do not.
    struct FlatRow {
        TreeNode *node = nullptr;
        int row = -1;
        QString rel;
    };

    QString relFor(const TreeNode *node, int proxyRow) const;
    void appendVisible(TreeNode *node, QList<FlatRow> &out) const;
    void connectNode(TreeNode *node);
    void destroySubtree(const QString &relPath);
    void clearChildNodes();
    void teardown();
    void structuralSync();
    void rebuildIndexes();
    void applySortTo(FileSortFilterModel *proxy) const;
    void forwardDataChanged(TreeNode *node, int first, int last, const QList<int> &roles);

    DirectoryModel *m_rootModel = nullptr;
    TreeNode *m_root = nullptr;
    QHash<QString, TreeNode *> m_nodes; // expanded nodes by relPath, root at ""
    QList<FlatRow> m_flat;
    QHash<QString, int> m_rowByName;
    QHash<const TreeNode *, QList<int>> m_flatByNodeRow; // proxy row -> flat row

    int m_sortKey = FileSortFilterModel::ByName;
    bool m_sortDescending = false;
    bool m_foldersFirst = true;
    bool m_showHidden = false;
    bool m_syncing = false;
};
