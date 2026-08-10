#include "SearchModel.h"
#include "DirectoryModel.h"
#include "Location.h"

namespace {

constexpr int kBatchSize = 256;
constexpr int kMaxParallelDirs = 4;
constexpr int kMaxResults = 1000;
constexpr int kMaxVisitedDirs = 20000;
constexpr int kDebounceMs = 250;

// The localsearch indexer's SPARQL endpoint on the session bus (Nautilus's
// Full Text search queries the same service).
const char kLocalSearchService[] = "org.freedesktop.LocalSearch3";

// ~match and ~prefix are bound per query — binding, not string-building, is
// what makes user input safe to pass through. tinysparql tokenizes the match
// string itself (stray FTS syntax like quotes and parens is tolerated) and
// prefix-matches each word. The LIMIT must stay kMaxResults.
const char kFtsQuery[] =
    "SELECT ?url WHERE {"
    "  ?ie fts:match ~match ."
    "  ?ie nie:isStoredAs ?f ."
    "  ?f nie:url ?url ."
    "  FILTER(STRSTARTS(STR(?url), ~prefix))"
    "} ORDER BY DESC(fts:rank(?ie)) LIMIT 1000";

// onQueryReady → onCursorNext accumulate rows across async steps; the struct
// owns the cursor reference until the walk ends.
struct CursorCtx {
    QPointer<SearchModel> self;
    quint64 generation = 0;
    TrackerSparqlCursor *cursor = nullptr;
    QStringList urls;
};

} // namespace

TrackerSparqlConnection *SearchModel::s_testConnection = nullptr;

void SearchModel::setConnectionForTesting(TrackerSparqlConnection *connection)
{
    s_testConnection = connection;
}

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Typing must not launch a tree walk per keystroke.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &SearchModel::restart);
}

SearchModel::~SearchModel()
{
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
    }
    clearQueue();
    if (m_sparql)
        g_object_unref(m_sparql);
}

void SearchModel::clearQueue()
{
    for (PendingDir &dir : m_queue)
        g_object_unref(dir.file);
    m_queue.clear();
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_results.size());
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
        return {};

    const Result &result = m_results.at(index.row());
    switch (role) {
    // The relative path, not the basename, for both identity and display:
    // name-keyed selection in Tab.qml needs uniqueness, and a results list
    // full of same-named files from different folders needs to say which is
    // which.
    case DirectoryModel::NameRole: return result.relPath;
    case DirectoryModel::DisplayNameRole: return result.relPath;
    case DirectoryModel::FilePathRole: return Location::descend(m_root, result.relPath);
    case DirectoryModel::IsDirRole: return result.entry.isDir;
    case DirectoryModel::IsHiddenRole: return result.entry.isHidden;
    case DirectoryModel::IsBackupRole: return result.entry.isBackup;
    case DirectoryModel::IsSymlinkRole: return result.entry.isSymlink;
    case DirectoryModel::SizeRole: return result.entry.size;
    case DirectoryModel::ModifiedRole: return result.entry.modified;
    case DirectoryModel::ContentTypeRole: return result.entry.contentType;
    case DirectoryModel::TypeDescriptionRole: return result.entry.typeDescription;
    case DirectoryModel::IconSourceRole:
        return QStringLiteral("image://fileicon/") + result.entry.iconNames.join(QLatin1Char(','));
    case DirectoryModel::OrigPathRole: return result.entry.origPath;
    case DirectoryModel::ItemCountRole: return result.entry.itemCount;
    case DirectoryModel::ItemCountAllRole: return result.entry.itemCountAll;
    case DirectoryModel::DepthRole: return 0;
    case DirectoryModel::ExpandedRole: return false;
    default: return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    // Byte-for-byte the DirectoryModel names, so FileSortFilterModel and the
    // views cannot tell which model is underneath.
    return {
        { DirectoryModel::NameRole, "name" },
        { DirectoryModel::DisplayNameRole, "displayName" },
        { DirectoryModel::FilePathRole, "filePath" },
        { DirectoryModel::IsDirRole, "isDir" },
        { DirectoryModel::IsHiddenRole, "isHidden" },
        { DirectoryModel::IsBackupRole, "isBackup" },
        { DirectoryModel::IsSymlinkRole, "isSymlink" },
        { DirectoryModel::SizeRole, "size" },
        { DirectoryModel::ModifiedRole, "modified" },
        { DirectoryModel::ContentTypeRole, "contentType" },
        { DirectoryModel::TypeDescriptionRole, "typeDescription" },
        { DirectoryModel::IconSourceRole, "iconSource" },
        { DirectoryModel::OrigPathRole, "origPath" },
        { DirectoryModel::ItemCountRole, "itemCount" },
        { DirectoryModel::ItemCountAllRole, "itemCountAll" },
        { DirectoryModel::DepthRole, "depth" },
        { DirectoryModel::ExpandedRole, "expanded" },
    };
}

void SearchModel::setRootLocation(const QString &location)
{
    const QString clean = Location::clean(location);
    if (clean == m_root)
        return;
    m_root = clean;
    Q_EMIT rootLocationChanged();
    if (!m_query.isEmpty())
        m_debounce.start();
}

void SearchModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    m_debounce.start();
}

void SearchModel::setContentMode(bool contentMode)
{
    if (contentMode == m_contentMode)
        return;
    m_contentMode = contentMode;
    Q_EMIT contentModeChanged();
    // A mode flip is one deliberate click, not typing — rerun immediately.
    m_debounce.stop();
    restart();
}

void SearchModel::setRecursion(const QString &recursion)
{
    if (m_recursion == recursion)
        return;
    m_recursion = recursion;
    Q_EMIT recursionChanged();
    // A policy flip mid-search changes what the walk may visit — re-run.
    if (m_searching || !m_results.isEmpty())
        restart();
}

void SearchModel::setDateKind(const QString &kind)
{
    static const QStringList allowed{ QStringLiteral("modified"), QStringLiteral("created"),
                                      QStringLiteral("accessed") };
    const QString clean = allowed.contains(kind) ? kind : allowed.first();
    if (m_dateKind == clean)
        return;
    m_dateKind = clean;
    Q_EMIT dateKindChanged();
    if (m_searching || !m_results.isEmpty())
        restart();
}

void SearchModel::setDateRange(const QString &range)
{
    static const QStringList allowed{ QStringLiteral("any"), QStringLiteral("today"),
                                      QStringLiteral("yesterday"), QStringLiteral("week"),
                                      QStringLiteral("month"), QStringLiteral("year") };
    const QString clean = allowed.contains(range) ? range : allowed.first();
    if (m_dateRange == clean)
        return;
    m_dateRange = clean;
    Q_EMIT dateRangeChanged();
    if (m_searching || !m_results.isEmpty())
        restart();
}

void SearchModel::setTypeFilter(const QString &filter)
{
    const QString clean = typeFilters().contains(filter) ? filter : QStringLiteral("any");
    if (m_typeFilter == clean)
        return;
    m_typeFilter = clean;
    Q_EMIT typeFilterChanged();
    if (m_searching || !m_results.isEmpty())
        restart();
}

QStringList SearchModel::typeFilters()
{
    // Nautilus's search-popover categories, in its menu order.
    return { QStringLiteral("any"), QStringLiteral("folders"), QStringLiteral("documents"),
             QStringLiteral("illustration"), QStringLiteral("music"), QStringLiteral("pdf"),
             QStringLiteral("pictures"), QStringLiteral("presentations"),
             QStringLiteral("spreadsheets"), QStringLiteral("text"), QStringLiteral("videos") };
}

namespace {

// True when `type` is (a subtype of) any of the category's members —
// g_content_type_is_a is what makes "text" match text/x-python and friends,
// the same containment Nautilus's filter uses.
bool typeIsAnyOf(const QString &type, std::initializer_list<const char *> members)
{
    const QByteArray utf8 = type.toUtf8();
    for (const char *member : members) {
        if (g_content_type_is_a(utf8.constData(), member))
            return true;
    }
    return false;
}

} // namespace

bool SearchModel::acceptsEntry(const FileEntry &entry) const
{
    if (m_typeFilter != QLatin1String("any")) {
        const QString &type = entry.contentType;
        bool matches = false;
        if (m_typeFilter == QLatin1String("folders"))
            matches = entry.isDir;
        else if (m_typeFilter == QLatin1String("music"))
            matches = type.startsWith(QLatin1String("audio/"));
        else if (m_typeFilter == QLatin1String("pictures"))
            matches = type.startsWith(QLatin1String("image/"));
        else if (m_typeFilter == QLatin1String("videos"))
            matches = type.startsWith(QLatin1String("video/"));
        else if (m_typeFilter == QLatin1String("text"))
            matches = typeIsAnyOf(type, { "text/plain" });
        else if (m_typeFilter == QLatin1String("pdf"))
            matches = typeIsAnyOf(type, { "application/pdf", "application/postscript",
                                          "application/x-dvi", "image/x-eps" });
        else if (m_typeFilter == QLatin1String("illustration"))
            matches = typeIsAnyOf(type, { "image/svg+xml", "application/illustrator",
                                          "image/x-xcf", "image/x-compressed-xcf",
                                          "image/x-psd",
                                          "application/vnd.oasis.opendocument.graphics" });
        else if (m_typeFilter == QLatin1String("documents"))
            matches = typeIsAnyOf(type, { "application/rtf", "application/msword",
                                          "application/vnd.oasis.opendocument.text",
                                          "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                                          "application/x-abiword",
                                          "application/vnd.sun.xml.writer",
                                          "application/vnd.apple.pages" });
        else if (m_typeFilter == QLatin1String("presentations"))
            matches = typeIsAnyOf(type, { "application/vnd.ms-powerpoint",
                                          "application/vnd.oasis.opendocument.presentation",
                                          "application/vnd.openxmlformats-officedocument.presentationml.presentation",
                                          "application/vnd.sun.xml.impress",
                                          "application/vnd.apple.keynote" });
        else if (m_typeFilter == QLatin1String("spreadsheets"))
            matches = typeIsAnyOf(type, { "application/vnd.ms-excel",
                                          "application/vnd.oasis.opendocument.spreadsheet",
                                          "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                                          "application/x-gnumeric",
                                          "application/vnd.apple.numbers" });
        if (!matches)
            return false;
    }

    if (m_dateRange != QLatin1String("any")) {
        const QDateTime stamp = m_dateKind == QLatin1String("created") ? entry.created
                              : m_dateKind == QLatin1String("accessed") ? entry.accessed
                              : entry.modified;
        // No timestamp cannot satisfy a date filter — a filesystem with no
        // birth times returning everything under "Created today" would lie.
        if (!stamp.isValid())
            return false;

        const QDateTime now = QDateTime::currentDateTime();
        QDateTime since; // the range is "since", as Nautilus's presets
        if (m_dateRange == QLatin1String("today"))
            since = QDateTime(QDate::currentDate(), QTime(0, 0));
        else if (m_dateRange == QLatin1String("yesterday"))
            since = QDateTime(QDate::currentDate().addDays(-1), QTime(0, 0));
        else if (m_dateRange == QLatin1String("week"))
            since = now.addDays(-7);
        else if (m_dateRange == QLatin1String("month"))
            since = now.addDays(-30);
        else // year
            since = now.addDays(-365);
        if (stamp < since)
            return false;
    }

    return true;
}

void SearchModel::setUnavailable(const QString &reason)
{
    if (m_unavailableReason == reason)
        return;
    m_unavailableReason = reason;
    Q_EMIT unavailableReasonChanged();
}

void SearchModel::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    Q_EMIT searchingChanged();
}

void SearchModel::restart()
{
    ++m_generation;
    if (m_cancellable) {
        g_cancellable_cancel(m_cancellable);
        g_object_unref(m_cancellable);
        m_cancellable = nullptr;
    }
    clearQueue();
    m_inFlight = 0;
    m_visitedDirs = 0;
    if (m_capped) {
        m_capped = false;
        Q_EMIT cappedChanged();
    }

    beginResetModel();
    m_results.clear();
    endResetModel();
    Q_EMIT countChanged();

    m_pendingContent.clear();
    m_statsInFlight = 0;
    setUnavailable({});

    m_needle = m_query.trimmed().toCaseFolded();
    if (m_needle.isEmpty() || m_root.isEmpty()) {
        setSearching(false);
        return;
    }

    setSearching(true);
    m_cancellable = g_cancellable_new();

    if (m_contentMode) {
        startContentSearch();
        return;
    }

    // Decided once per search, not per directory: whether the walk may leave
    // the root folder at all.
    m_descend = m_recursion == QLatin1String("always")
        || (m_recursion != QLatin1String("never") && Location::isLocal(m_root));

    m_queue.append({ Location::make(m_root), QString() });
    pump();
}

// --- content (full-text) search ---------------------------------------------

void SearchModel::startContentSearch()
{
    // The index speaks file:// only — content search over trash:// or a
    // mount would return nothing however it was asked.
    if (!Location::isLocal(m_root)) {
        setUnavailable(QStringLiteral("Content search works in local folders only"));
        setSearching(false);
        return;
    }

    if (!m_sparql && s_testConnection) {
        m_sparql = static_cast<TrackerSparqlConnection *>(g_object_ref(s_testConnection));
    }

    if (m_sparqlFailed) {
        setUnavailable(QStringLiteral("Content search is unavailable — no search index"));
        setSearching(false);
        return;
    }

    if (m_sparql) {
        executeContentQuery();
        return;
    }

    if (m_connecting)
        return; // the pending connect runs the current query when it lands

    m_connecting = true;
    // Deliberately not the generation cancellable: the connection outlives
    // any one query, and a superseded search must not tear it down.
    tracker_sparql_connection_bus_new_async(kLocalSearchService, nullptr, nullptr, nullptr,
                                            &SearchModel::onConnectionReady,
                                            new QPointer<SearchModel>(this));
}

void SearchModel::onConnectionReady(GObject *, GAsyncResult *res, gpointer data)
{
    auto *guard = static_cast<QPointer<SearchModel> *>(data);
    GError *error = nullptr;
    TrackerSparqlConnection *conn = tracker_sparql_connection_bus_new_finish(res, &error);

    SearchModel *self = guard->data();
    delete guard;
    if (!self) {
        if (conn)
            g_object_unref(conn);
        g_clear_error(&error);
        return;
    }

    self->m_connecting = false;
    if (!conn) {
        g_clear_error(&error);
        self->m_sparqlFailed = true;
        if (self->m_searching && self->m_contentMode) {
            self->setUnavailable(QStringLiteral("Content search is unavailable — no search index"));
            self->setSearching(false);
        }
        return;
    }

    self->m_sparql = conn;
    // Whatever the query is *now* — restarts during the connect are fine,
    // the newest generation is the one still searching.
    if (self->m_searching && self->m_contentMode && !self->m_needle.isEmpty())
        self->executeContentQuery();
}

void SearchModel::executeContentQuery()
{
    GError *error = nullptr;
    TrackerSparqlStatement *stmt =
        tracker_sparql_connection_query_statement(m_sparql, kFtsQuery, nullptr, &error);
    if (!stmt) {
        g_clear_error(&error);
        setUnavailable(QStringLiteral("Content search is unavailable — no search index"));
        setSearching(false);
        return;
    }

    GFile *rootFile = Location::make(m_root);
    char *rootUri = g_file_get_uri(rootFile);
    const QString prefix = QString::fromUtf8(rootUri) + QLatin1Char('/');
    g_free(rootUri);
    g_object_unref(rootFile);

    // The trimmed query, not the case-folded needle — the index does its own
    // case folding, and binding (not concatenation) does the escaping.
    tracker_sparql_statement_bind_string(stmt, "match", m_query.trimmed().toUtf8().constData());
    tracker_sparql_statement_bind_string(stmt, "prefix", prefix.toUtf8().constData());

    auto *ctx = new CursorCtx{ this, m_generation, nullptr, {} };
    tracker_sparql_statement_execute_async(stmt, m_cancellable,
                                           &SearchModel::onQueryReady, ctx);
    g_object_unref(stmt); // execute holds its own reference
}

void SearchModel::onQueryReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CursorCtx *>(data);
    GError *error = nullptr;
    TrackerSparqlCursor *cursor =
        tracker_sparql_statement_execute_finish(TRACKER_SPARQL_STATEMENT(source), res, &error);

    SearchModel *self = ctx->self.data();
    if (!self || ctx->generation != self->m_generation) {
        if (cursor) {
            tracker_sparql_cursor_close(cursor);
            g_object_unref(cursor);
        }
        g_clear_error(&error);
        delete ctx;
        return;
    }

    if (!cursor) {
        g_clear_error(&error);
        self->setUnavailable(QStringLiteral("Content search is unavailable — no search index"));
        self->setSearching(false);
        delete ctx;
        return;
    }

    ctx->cursor = cursor;
    tracker_sparql_cursor_next_async(cursor, self->m_cancellable,
                                     &SearchModel::onCursorNext, ctx);
}

void SearchModel::onCursorNext(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<CursorCtx *>(data);
    auto *cursor = TRACKER_SPARQL_CURSOR(source);
    GError *error = nullptr;
    const gboolean more = tracker_sparql_cursor_next_finish(cursor, res, &error);

    SearchModel *self = ctx->self.data();
    if (!self || ctx->generation != self->m_generation) {
        g_clear_error(&error);
        tracker_sparql_cursor_close(cursor);
        g_object_unref(cursor);
        delete ctx;
        return;
    }

    if (more) {
        ctx->urls.append(QString::fromUtf8(tracker_sparql_cursor_get_string(cursor, 0, nullptr)));
        tracker_sparql_cursor_next_async(cursor, self->m_cancellable,
                                         &SearchModel::onCursorNext, ctx);
        return;
    }

    // Done (or errored — either way this cursor is finished).
    g_clear_error(&error);
    tracker_sparql_cursor_close(cursor);
    g_object_unref(cursor);

    if (ctx->urls.size() >= kMaxResults && !self->m_capped) {
        self->m_capped = true;
        Q_EMIT self->cappedChanged();
    }

    self->statContentHits(ctx->urls);
    delete ctx;
}

void SearchModel::statContentHits(const QStringList &urls)
{
    // Root with the trailing separator, for prefix-stripping into relPath.
    const QString rootPrefix = m_root.endsWith(QLatin1Char('/')) ? m_root
                                                                 : m_root + QLatin1Char('/');

    for (const QString &url : urls) {
        GFile *file = g_file_new_for_uri(url.toUtf8().constData());
        char *cPath = g_file_get_path(file);
        const QString path = cPath ? QString::fromUtf8(cPath) : QString();
        g_free(cPath);

        if (!path.startsWith(rootPrefix)) {
            // This check is the scoping contract (tst_fts pins it here). The
            // SPARQL FILTER upstream is not redundant though: without it,
            // out-of-scope matches could eat the query's LIMIT before any
            // in-scope row arrived.
            g_object_unref(file);
            continue;
        }

        auto *ctx = new StatCtx{ this, m_generation, path.mid(rootPrefix.size()) };
        ++m_statsInFlight;
        g_file_query_info_async(file, FileEntry::queryAttributes(),
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                G_PRIORITY_DEFAULT, m_cancellable,
                                &SearchModel::onStatReady, ctx);
        g_object_unref(file);
    }

    if (m_statsInFlight == 0)
        finishContentStat();
}

void SearchModel::onStatReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<StatCtx *>(data);
    GError *error = nullptr;
    GFileInfo *info = g_file_query_info_finish(G_FILE(source), res, &error);

    SearchModel *self = ctx->self.data();
    if (!self || ctx->generation != self->m_generation) {
        if (info)
            g_object_unref(info);
        g_clear_error(&error);
        delete ctx;
        return;
    }

    if (info) {
        // relPath as the entry name mirrors the walk's convention exactly.
        // The filters apply to content hits too — the stat is where a hit
        // first has the attributes to judge.
        FileEntry entry = FileEntry::fromInfo(info);
        if (self->acceptsEntry(entry))
            self->m_pendingContent.append({ std::move(entry), ctx->relPath });
        g_object_unref(info);
    } else {
        // Indexed but gone from disk — the index just hasn't caught up.
        g_clear_error(&error);
    }

    --self->m_statsInFlight;
    if (self->m_statsInFlight == 0)
        self->finishContentStat();
    delete ctx;
}

void SearchModel::finishContentStat()
{
    if (!m_pendingContent.isEmpty()) {
        QList<Result> batch = std::move(m_pendingContent);
        m_pendingContent = {};
        if (batch.size() > kMaxResults)
            batch = batch.mid(0, kMaxResults);
        beginInsertRows({}, 0, int(batch.size()) - 1);
        m_results = std::move(batch);
        endInsertRows();
        Q_EMIT countChanged();
    }
    setSearching(false);
}

void SearchModel::pump()
{
    while (m_inFlight < kMaxParallelDirs && !m_queue.isEmpty()) {
        if (m_visitedDirs >= kMaxVisitedDirs || m_results.size() >= kMaxResults) {
            if (!m_capped) {
                m_capped = true;
                Q_EMIT cappedChanged();
            }
            clearQueue();
            break;
        }

        PendingDir dir = m_queue.takeFirst();
        ++m_visitedDirs;
        ++m_inFlight;

        auto *ctx = new EnumerateCtx{ this, m_generation, dir.relPrefix };
        g_file_enumerate_children_async(dir.file, FileEntry::queryAttributes(),
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        G_PRIORITY_DEFAULT, m_cancellable,
                                        &SearchModel::onEnumerateReady, ctx);
        g_object_unref(dir.file);
    }

    if (m_inFlight == 0)
        setSearching(false);
}

void SearchModel::finishDir()
{
    --m_inFlight;
    pump();
}

void SearchModel::onEnumerateReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<EnumerateCtx *>(data);
    GError *error = nullptr;
    GFileEnumerator *enumerator = g_file_enumerate_children_finish(G_FILE(source), res, &error);

    SearchModel *self = ctx->self.data();
    if (!self || ctx->generation != self->m_generation) {
        // A superseded search (or a dead model): nothing to update — for a
        // supersede, restart() already zeroed the counters for the new
        // generation.
        if (enumerator)
            g_object_unref(enumerator);
        g_clear_error(&error);
        delete ctx;
        return;
    }

    if (!enumerator) {
        // An unreadable directory is an expected part of a tree walk
        // (permissions), not a failed search.
        g_clear_error(&error);
        self->finishDir();
        delete ctx;
        return;
    }

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_DEFAULT,
                                       self->m_cancellable,
                                       &SearchModel::onNextFilesReady, ctx);
}

void SearchModel::onNextFilesReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<EnumerateCtx *>(data);
    auto *enumerator = G_FILE_ENUMERATOR(source);

    GError *error = nullptr;
    GList *infos = g_file_enumerator_next_files_finish(enumerator, res, &error);

    SearchModel *self = ctx->self.data();
    if (!self || ctx->generation != self->m_generation) {
        g_list_free_full(infos, g_object_unref);
        g_clear_error(&error);
        g_object_unref(enumerator);
        delete ctx;
        return;
    }

    if (!infos) {
        g_clear_error(&error);
        g_object_unref(enumerator);
        self->finishDir();
        delete ctx;
        return;
    }

    self->consume(infos, ctx->relPrefix);
    g_list_free_full(infos, g_object_unref);

    g_file_enumerator_next_files_async(enumerator, kBatchSize, G_PRIORITY_DEFAULT,
                                       self->m_cancellable,
                                       &SearchModel::onNextFilesReady, ctx);
}

void SearchModel::consume(GList *infos, const QString &relPrefix)
{
    QList<Result> matched;

    for (GList *l = infos; l; l = l->next) {
        FileEntry entry = FileEntry::fromInfo(static_cast<GFileInfo *>(l->data));
        if (entry.isHidden || entry.isBackup)
            continue;

        const QString relPath = relPrefix + entry.name;

        // Descend into real directories only — symlinks can loop.
        if (m_descend && entry.isDir && !entry.isSymlink && m_visitedDirs < kMaxVisitedDirs) {
            GFile *child = Location::make(Location::descend(m_root, relPath));
            m_queue.append({ child, relPath + QLatin1Char('/') });
        }

        if (entry.displayName.toCaseFolded().contains(m_needle) && acceptsEntry(entry))
            matched.append({ std::move(entry), relPath });
    }

    if (!matched.isEmpty() && m_results.size() < kMaxResults) {
        if (m_results.size() + matched.size() > kMaxResults)
            matched = matched.mid(0, kMaxResults - m_results.size());
        beginInsertRows({}, int(m_results.size()),
                        int(m_results.size() + matched.size()) - 1);
        m_results += matched;
        endInsertRows();
        Q_EMIT countChanged();
    }

    // New directories may be waiting and slots may be free.
    pump();
}
