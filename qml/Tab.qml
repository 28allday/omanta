import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// One tab: a location, its history, its view state and its selection.
// Everything a tab knows lives here, which is what lets windows hold several
// independent ones and lets a new window start from a clean copy.
FocusScope {
    id: root

    property string path: ""
    property string pendingSelection: ""
    // Non-empty turns the tab into search results for this query under
    // `path`. The window's search bar owns the editing; this is the state.
    property string searchQuery: ""
    // Full-text search against the localsearch index instead of the
    // filename walk. Sticky per tab, like the view mode.
    property bool searchContent: false
    // The search-filter popover's state — sticky per tab, like searchContent.
    property string searchDateKind: "modified"
    property string searchDateRange: "any"
    property string searchTypeFilter: "any"

    // Expandable folders (Nautilus's use-tree-view) only make sense over a
    // real directory listing in the list view; everywhere else the flat
    // proxy serves as always.
    readonly property bool treeActive: viewMode === "list" && Settings.useTreeView
        && searchQuery === "" && !viewingStarred && !viewingNetwork
    // The model the views and every helper below speak to. The two carry the
    // same roles and the same invokable surface, so nothing downstream knows
    // which is live.
    readonly property var files: treeActive ? treeModel : proxy
    readonly property alias history: history
    readonly property string title: path === "/" ? "/" : Platform.baseName(path)
    readonly property int selectionCount: Object.keys(selectedNames).length
    readonly property bool searching: searchQuery !== "" && searchModel.searching
    readonly property string statusText: {
        if (searchQuery !== "") {
            if (searchModel.unavailableReason)
                return searchModel.unavailableReason;
            const found = files.count === 1 ? "1 result" : files.count + " results";
            if (searchModel.searching)
                return "Searching — " + found + " so far";
            return searchModel.capped ? found + " (stopped early — narrow the search)"
                                      : found;
        }
        if (dirModel.errorMessage)
            return dirModel.errorMessage;
        if (dirModel.loading)
            return "Loading…";
        const total = files.count;
        const items = total === 1 ? "1 item" : total + " items";
        return selectionCount > 0 ? items + ", " + selectionCount + " selected" : items;
    }

    // View state. The default follows the Settings store, which switching
    // views writes back — Nautilus's default-folder-viewer behaviour.
    property string viewMode: Settings.defaultViewMode
    property int zoom: 64
    property bool showHidden: false
    property int sortKey: FileSortFilterModel.ByName
    property bool sortDescending: false

    // Selection is a plain set keyed by filename. Names are stable within a
    // directory, so a selection survives re-sorting and in-place model updates
    // that row indices would not.
    property var selectedNames: ({})
    property int currentIndex: -1
    property int anchorIndex: -1

    signal contextMenuRequested()
    // The tab's location needs mounting first — the window runs the mount
    // (and its credential dialog) and reloads on success.
    signal mountNeeded(string location)
    // A drop decided what should move or copy where; the window runs it
    // through the same conflict handling as paste.
    signal transferRequested(var sources, string destination, bool isMove)

    readonly property bool viewingStarred: path === "starred:///"
    readonly property bool viewingNetwork: path === "network:///"
    // Recent rows are pointers: recent:///<id> URIs that no file operation
    // can act on. Everything that leaves this tab — selection, activation,
    // drags — resolves them to the target file, as Nautilus does. Trash rows
    // also carry a target-uri (into Trash/files) but must NOT be resolved:
    // restore and delete need the trash:// URI.
    readonly property bool viewingRecent: path === "recent:///"

    // Batch rename works on a real directory listing only: search results,
    // starred and network rows come from many places (or aren't files), so
    // one folder's name list can't answer their conflicts. A tree selection
    // spans folders, which is the same problem.
    readonly property bool batchRenamable: searchQuery === "" && !viewingStarred
        && !viewingNetwork && !treeActive
        && path !== "trash:///" && path !== "recent:///"

    DirectoryModel {
        id: dirModel
        // starred:/// and network:/// are ours, not GIO's — the directory
        // model must not be asked to enumerate them.
        path: root.viewingStarred || root.viewingNetwork ? "" : root.path
        // Same three-way policy shape as thumbnails: a remote mount only
        // counts folders when the preference says all locations.
        countItems: Settings.showDirectoryItemCounts === "always"
                 || (Settings.showDirectoryItemCounts === "local-only"
                     && Platform.isLocal(root.path))
        onNeedsMount: location => root.mountNeeded(location)
    }

    SearchModel {
        id: searchModel
        rootLocation: root.path
        query: root.searchQuery
        contentMode: root.searchContent
        recursion: Settings.searchInSubfolders
        dateKind: root.searchDateKind
        dateRange: root.searchDateRange
        typeFilter: root.searchTypeFilter
    }

    StarredModel {
        id: starredModel
        store: StarredStore
        active: root.viewingStarred
    }

    NetworkModel {
        id: networkModel
        store: ServerStore
        active: root.viewingNetwork
    }

    FileSortFilterModel {
        id: proxy
        // The views never know which model is underneath — search results,
        // the starred listing and the network view speak the same roles,
        // sort by the same columns, and keep selection working because
        // their name role is unique (relative path / URI respectively).
        sourceModel: root.searchQuery !== "" ? searchModel
                   : root.viewingStarred ? starredModel
                   : root.viewingNetwork ? networkModel : dirModel
        showHidden: root.showHidden
        sortKey: root.sortKey
        sortDescending: root.sortDescending
        // Nautilus preference, false out of the box: folders sort with the
        // files unless the user asks otherwise.
        foldersFirst: Settings.sortFoldersFirst
    }

    DirectoryTreeModel {
        id: treeModel
        // The tab's own dirModel is the root — one listing, one monitor,
        // whichever projection is on screen.
        rootModel: dirModel
        sortKey: root.sortKey
        sortDescending: root.sortDescending
        foldersFirst: Settings.sortFoldersFirst
        showHidden: root.showHidden
    }

    NavigationHistory {
        id: history
    }

    Component.onCompleted: history.visit(root.path)

    onPathChanged: {
        clearSelection();
        currentIndex = -1;
        searchQuery = ""; // navigating away is leaving the search
        if (history.current !== path)
            history.visit(path);
    }

    onSearchQueryChanged: {
        clearSelection();
        currentIndex = -1;
    }

    // A path handed in from the CLI or a D-Bus reveal names a file to select,
    // but the listing has to exist first.
    Connections {
        target: dirModel
        function onLoadingChanged() {
            if (dirModel.loading || !root.pendingSelection)
                return;
            const row = files.proxyRowForName(root.pendingSelection);
            root.pendingSelection = "";
            if (row >= 0) {
                root.selectOnly(files.valueAt(row, "name"));
                root.currentIndex = row;
                root.positionAt(row);
            }
        }
    }

    // ---- navigation -------------------------------------------------------

    function navigate(target) {
        if (!target)
            return;
        // A local path is checked synchronously; a URI is taken on trust —
        // stat-ing smb:// here would freeze the UI on the network, and the
        // model reports an unreachable location in the status line anyway.
        if (!Platform.isNavigable(target))
            return;
        root.path = target;
    }

    function goBack() {
        const target = history.goBack();
        if (target)
            root.path = target;
    }

    function goForward() {
        const target = history.goForward();
        if (target)
            root.path = target;
    }

    function goUp() {
        const parent = Platform.parentPath(root.path);
        if (parent)
            navigate(parent);
    }

    function activate(row) {
        if (row < 0 || row >= files.count)
            return;
        const target = actionPathAt(row);
        if (files.valueAt(row, "isDir")) {
            navigate(target);
        } else if (Platform.activationExtracts(files.valueAt(row, "contentType"))) {
            // Nautilus 50: opening an archive extracts it, but only when the
            // file manager itself is the type's default handler (post-cutover
            // here). Until then the default app gets it like any other file.
            FileOperations.extractHere([target], root.path);
        } else {
            Platform.openPath(target);
        }
    }

    function reload() { dirModel.reload(); }

    // A drop landed. Turns the drag's URLs into locations, applies the
    // Nautilus modifier convention — Ctrl forces copy, Shift forces move,
    // unmodified moves within a filesystem and copies across one — and hands
    // the transfer up. Modifiers are read now, not from the drop event,
    // because QML drop events do not carry them.
    function requestDrop(urls, destination) {
        const paths = Platform.locationsFromUrls(urls);
        if (paths.length === 0 || !destination)
            return;
        // A folder cannot be dropped into itself.
        if (paths.indexOf(destination) >= 0)
            return;

        const mods = Platform.keyboardModifiers();
        let isMove;
        if (mods & Qt.ControlModifier)
            isMove = false;
        else if (mods & Qt.ShiftModifier)
            isMove = true;
        else
            isMove = Platform.sameFilesystem(paths[0], destination);

        // Moving things into the folder they are already in is a no-op, not
        // an operation with a conflict dialog.
        if (isMove && paths.every(p => Platform.parentPath(p) === destination))
            return;

        root.transferRequested(paths, destination, isMove);
    }

    // ---- selection --------------------------------------------------------

    function isSelected(name) { return selectedNames[name] === true; }

    function clearSelection() {
        selectedNames = ({});
        anchorIndex = -1;
    }

    function selectOnly(name) {
        const next = {};
        next[name] = true;
        selectedNames = next;
        anchorIndex = files.proxyRowForName(name);
    }

    function toggleSelection(name) {
        const next = Object.assign({}, selectedNames);
        if (next[name])
            delete next[name];
        else
            next[name] = true;
        selectedNames = next;
        anchorIndex = files.proxyRowForName(name);
    }

    function selectNames(names) {
        const next = {};
        for (let i = 0; i < names.length; ++i)
            next[names[i]] = true;
        selectedNames = next;
    }

    function selectRange(first, last) {
        const next = {};
        for (let row = Math.max(0, first); row <= Math.min(files.count - 1, last); ++row)
            next[files.valueAt(row, "name")] = true;
        selectedNames = next;
    }

    function extendSelectionTo(row) {
        if (anchorIndex < 0)
            anchorIndex = row;
        selectRange(Math.min(anchorIndex, row), Math.max(anchorIndex, row));
    }

    function selectAll() {
        const next = {};
        for (let row = 0; row < files.count; ++row)
            next[files.valueAt(row, "name")] = true;
        selectedNames = next;
    }

    // The path operations should act on for a row — the target file when
    // this is the Recent view, the row itself everywhere else.
    function actionPathAt(row) {
        if (viewingRecent) {
            const target = files.valueAt(row, "targetPath");
            if (target !== "")
                return target;
        }
        return files.valueAt(row, "filePath");
    }

    function selectedPaths() {
        const paths = [];
        for (let row = 0; row < files.count; ++row) {
            const name = files.valueAt(row, "name");
            if (selectedNames[name])
                paths.push(actionPathAt(row));
        }
        return paths;
    }

    // What the batch rename dialog works from: the selected rows with the
    // fields name generation needs, in display order.
    function selectedItems() {
        const items = [];
        for (let row = 0; row < files.count; ++row) {
            const name = files.valueAt(row, "name");
            if (selectedNames[name]) {
                items.push({ path: files.valueAt(row, "filePath"),
                             name: name,
                             modified: files.valueAt(row, "modified"),
                             isDir: files.valueAt(row, "isDir") });
            }
        }
        return items;
    }

    function allNames() { return dirModel.allNames(); }

    // Whether every selected row is an archive the engine can open — the
    // "Extract Here" gate.
    function selectionAllArchives() {
        let any = false;
        for (let row = 0; row < files.count; ++row) {
            const name = files.valueAt(row, "name");
            if (!selectedNames[name])
                continue;
            if (!Platform.isArchiveType(files.valueAt(row, "contentType")))
                return false;
            any = true;
        }
        return any;
    }

    // Where the selected trash rows came from — the paths Restore puts back.
    // Rows with no recorded origin (rare, but the spec allows it) are skipped.
    function selectedOrigPaths() {
        const paths = [];
        for (let row = 0; row < files.count; ++row) {
            const name = files.valueAt(row, "name");
            if (selectedNames[name]) {
                const orig = files.valueAt(row, "origPath");
                if (orig)
                    paths.push(orig);
            }
        }
        return paths;
    }

    function requestContextMenu() { root.contextMenuRequested(); }

    // ---- view helpers -----------------------------------------------------

    function positionAt(row) {
        if (viewLoader.item && viewLoader.item.positionAt)
            viewLoader.item.positionAt(row);
    }

    function setCurrent(row, extend) {
        if (row < 0 || row >= files.count)
            return;
        currentIndex = row;
        if (extend)
            extendSelectionTo(row);
        else
            selectOnly(files.valueAt(row, "name"));
        positionAt(row);
    }

    function moveCurrent(delta, extend) {
        const base = currentIndex < 0 ? (delta > 0 ? -1 : files.count) : currentIndex;
        setCurrent(Math.max(0, Math.min(files.count - 1, base + delta)), extend);
    }

    function setSort(key) {
        if (sortKey === key)
            sortDescending = !sortDescending;
        else {
            sortKey = key;
            sortDescending = false;
        }
    }

    function setZoom(value) { zoom = Math.max(32, Math.min(128, value)); }

    // The list view's expander click lands here; a no-op when the tree is off.
    function toggleExpand(row) {
        if (treeActive)
            files.toggleExpanded(row);
    }

    // Columns only mean something in the grid; the list is one per row.
    readonly property int viewColumns: viewMode === "icon" && viewLoader.item
                                       ? viewLoader.item.columns : 1

    // The list view's live column ids — what is actually rendered, not what
    // Settings claims, so verify-ui asserts the whole chain.
    readonly property var listColumns: viewMode === "list" && viewLoader.item
                                       ? viewLoader.item.listColumns : []

    // Same for the icon view's captions: the rendered set, after the
    // zoom-graded reveal has had its say.
    readonly property var iconCaptions: viewMode === "icon" && viewLoader.item
                                        ? viewLoader.item.captions : []

    // The current row's rendered Size cell ("12 items" for a folder) — what
    // verify-ui reads to assert directory item counts.
    readonly property string currentSizeCell: viewMode === "list" && viewLoader.item
                                              ? viewLoader.item.currentSizeCell : ""

    // The current row's tree state, likewise read from the live delegate.
    readonly property int currentDepth: viewMode === "list" && viewLoader.item
                                        ? viewLoader.item.currentDepth : 0
    readonly property bool currentExpanded: viewMode === "list" && viewLoader.item
                                            ? viewLoader.item.currentExpanded : false

    // ---- keyboard ---------------------------------------------------------

    Keys.onPressed: event => {
        const extend = (event.modifiers & Qt.ShiftModifier) !== 0;

        switch (event.key) {
        case Qt.Key_Down:
            moveCurrent(viewColumns, extend); event.accepted = true; return;
        case Qt.Key_Up:
            moveCurrent(-viewColumns, extend); event.accepted = true; return;
        case Qt.Key_Right:
            if (viewMode === "icon") { moveCurrent(1, extend); event.accepted = true; }
            else if (treeActive && currentIndex >= 0) {
                // GTK tree keys: Right expands a folder; on one already
                // expanded it steps into the first child.
                if (files.valueAt(currentIndex, "isDir")
                    && !files.valueAt(currentIndex, "expanded"))
                    files.expand(currentIndex);
                else if (files.valueAt(currentIndex, "expanded"))
                    moveCurrent(1, false);
                event.accepted = true;
            }
            return;
        case Qt.Key_Left:
            if (viewMode === "icon") { moveCurrent(-1, extend); event.accepted = true; }
            else if (treeActive && currentIndex >= 0) {
                // …and Left collapses, or from a plain row jumps to its parent.
                if (files.valueAt(currentIndex, "expanded"))
                    files.collapse(currentIndex);
                else {
                    const name = files.valueAt(currentIndex, "name") || "";
                    const cut = name.lastIndexOf("/");
                    if (cut > 0) {
                        const parentRow = files.proxyRowForName(name.slice(0, cut));
                        if (parentRow >= 0)
                            setCurrent(parentRow, false);
                    }
                }
                event.accepted = true;
            }
            return;
        case Qt.Key_Home:
            setCurrent(0, extend); event.accepted = true; return;
        case Qt.Key_End:
            setCurrent(files.count - 1, extend); event.accepted = true; return;
        case Qt.Key_PageDown:
            moveCurrent(10 * viewColumns, extend); event.accepted = true; return;
        case Qt.Key_PageUp:
            moveCurrent(-10 * viewColumns, extend); event.accepted = true; return;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            activate(currentIndex); event.accepted = true; return;
        case Qt.Key_Backspace:
            goUp(); event.accepted = true; return;
        case Qt.Key_Escape:
            clearSelection(); event.accepted = true; return;
        case Qt.Key_Menu:
            requestContextMenu(); event.accepted = true; return;
        }

        // Type-ahead. Modifier-free printable text jumps to the next match,
        // continuing the current prefix if the user is still typing.
        if (event.text.length > 0 && event.text.charCodeAt(0) >= 0x20
            && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            typeAhead.append(event.text);
            event.accepted = true;
        }
    }

    QtObject {
        id: typeAhead

        property string prefix: ""

        function append(text) {
            prefix += text;
            resetTimer.restart();
            const from = prefix.length === text.length ? root.currentIndex + 1 : root.currentIndex;
            const row = files.findByPrefix(prefix, Math.max(0, from));
            if (row >= 0)
                root.setCurrent(row, false);
        }
    }

    Timer {
        id: resetTimer
        interval: 900
        onTriggered: typeAhead.prefix = ""
    }

    // ---- the view itself --------------------------------------------------

    Loader {
        id: viewLoader

        anchors.fill: parent
        focus: true
        sourceComponent: root.viewMode === "icon" ? iconViewComponent : listViewComponent
    }

    Component {
        id: listViewComponent
        FileListView { tab: root; currentIndex: root.currentIndex }
    }

    Component {
        id: iconViewComponent
        FileIconView { tab: root; currentIndex: root.currentIndex }
    }

    // Nautilus's Network empty state — without it, an empty network view is
    // indistinguishable from a broken one.
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: root.viewingNetwork && files.count === 0

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: Colors.tint("image://fileicon/network-server", Colors.textDim)
            sourceSize: Qt.size(96, 96)
            opacity: 0.6
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("No Known Connections")
            color: Colors.text
            font.pixelSize: 20
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Enter an address to connect to a network location.")
            color: Colors.textDim
            font.pixelSize: 13
        }
    }
}
