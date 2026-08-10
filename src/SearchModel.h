#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration>

#include <gio/gio.h>
#include <tinysparql.h>

#include "FileEntry.h"

// Recursive filename search under one location. Feeds the same
// FileSortFilterModel and views as DirectoryModel by speaking the same roles —
// with one deliberate difference: the `name` role is the result's path
// relative to the search root. Within one search that is unique, which is
// what keeps name-keyed selection and type-ahead working over results drawn
// from many directories that may share basenames.
//
// Breadth-first, a few directories in flight at once, everything cancellable
// by generation. Hidden and backup files are skipped, symlinked directories
// are not descended into (loops), and both the result count and the visited
// directory count are capped — a search of "/" must end, not wedge the app.
//
// contentMode switches the same model to full-text search: a SPARQL query
// against the localsearch index over D-Bus, scoped under the root by URL
// prefix, with each hit stat'ed through GIO so the rows carry the same
// attributes the walk produces. The index also full-text-indexes filenames,
// so content mode matches names too — exactly what Nautilus's Full Text
// search does, because it queries the same index. Two consequences worth
// knowing: results only exist where the index does (localsearch skips
// git/hg repos and hidden trees), and a hit whose file has since been
// deleted is dropped when its stat fails.
class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootLocation READ rootLocation WRITE setRootLocation NOTIFY rootLocationChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool contentMode READ contentMode WRITE setContentMode NOTIFY contentModeChanged)
    // Nautilus's recursive-search preference: "never" searches only the
    // folder itself, "local-only" recurses on local filesystems but not over
    // a mount, "always" recurses everywhere.
    Q_PROPERTY(QString recursion READ recursion WRITE setRecursion NOTIFY recursionChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool capped READ capped NOTIFY cappedChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY unavailableReasonChanged)
    // Nautilus's search-filter popover, model side. dateRange is a "since"
    // window ("any" | "today" | "yesterday" | "week" | "month" | "year"),
    // dateKind picks which timestamp it applies to ("modified" | "created" |
    // "accessed" — Nautilus's Last Modified / Created / Last Used), and
    // typeFilter is one of the Nautilus mime categories (see typeFilters()).
    Q_PROPERTY(QString dateKind READ dateKind WRITE setDateKind NOTIFY dateKindChanged)
    Q_PROPERTY(QString dateRange READ dateRange WRITE setDateRange NOTIFY dateRangeChanged)
    Q_PROPERTY(QString typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)

public:
    explicit SearchModel(QObject *parent = nullptr);
    ~SearchModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootLocation() const { return m_root; }
    void setRootLocation(const QString &location);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool contentMode() const { return m_contentMode; }
    void setContentMode(bool contentMode);
    QString recursion() const { return m_recursion; }
    void setRecursion(const QString &recursion);
    QString dateKind() const { return m_dateKind; }
    void setDateKind(const QString &kind);
    QString dateRange() const { return m_dateRange; }
    void setDateRange(const QString &range);
    QString typeFilter() const { return m_typeFilter; }
    void setTypeFilter(const QString &filter);

    // The category ids in menu order — the QML layer owns the labels.
    Q_INVOKABLE static QStringList typeFilters();
    bool searching() const { return m_searching; }
    int count() const { return int(m_results.size()); }
    bool capped() const { return m_capped; }
    QString unavailableReason() const { return m_unavailableReason; }

    // Tests point every model at a private in-process store instead of the
    // session's localsearch. The model takes its own reference.
    static void setConnectionForTesting(TrackerSparqlConnection *connection);

Q_SIGNALS:
    void rootLocationChanged();
    void queryChanged();
    void contentModeChanged();
    void recursionChanged();
    void dateKindChanged();
    void dateRangeChanged();
    void typeFilterChanged();
    void searchingChanged();
    void countChanged();
    void cappedChanged();
    void unavailableReasonChanged();

private:
    struct Result {
        FileEntry entry;
        QString relPath; // unique within the search — doubles as the name role
    };

    struct PendingDir {
        GFile *file = nullptr;
        QString relPrefix;
    };

    struct EnumerateCtx {
        QPointer<SearchModel> self;
        quint64 generation = 0;
        QString relPrefix;
    };

    struct StatCtx {
        QPointer<SearchModel> self;
        quint64 generation = 0;
        QString relPath;
    };

    static void onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onNextFilesReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onConnectionReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onQueryReady(GObject *source, GAsyncResult *res, gpointer data);
    static void onCursorNext(GObject *source, GAsyncResult *res, gpointer data);
    static void onStatReady(GObject *source, GAsyncResult *res, gpointer data);

    void restart();
    void pump();
    bool acceptsEntry(const FileEntry &entry) const;
    void consume(GList *infos, const QString &relPrefix);
    void finishDir();
    void setSearching(bool searching);
    void setUnavailable(const QString &reason);
    void clearQueue();

    void startContentSearch();
    void executeContentQuery();
    void statContentHits(const QStringList &urls);
    void finishContentStat();

    QString m_root;
    QString m_query;
    QString m_needle; // trimmed, case-folded
    QList<Result> m_results;
    QList<PendingDir> m_queue;
    bool m_searching = false;
    bool m_capped = false;
    int m_inFlight = 0;
    int m_visitedDirs = 0;
    quint64 m_generation = 0;
    GCancellable *m_cancellable = nullptr;
    QTimer m_debounce;

    bool m_contentMode = false;
    QString m_recursion = QStringLiteral("local-only");
    QString m_dateKind = QStringLiteral("modified");
    QString m_dateRange = QStringLiteral("any");
    QString m_typeFilter = QStringLiteral("any");
    bool m_descend = true; // computed per search from m_recursion + the root
    QString m_unavailableReason;
    TrackerSparqlConnection *m_sparql = nullptr;
    bool m_sparqlFailed = false;
    bool m_connecting = false;
    QList<Result> m_pendingContent;
    int m_statsInFlight = 0;

    static TrackerSparqlConnection *s_testConnection;
};
