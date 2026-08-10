import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// A window. Several tabs, one visible at a time, plus the chrome that acts on
// whichever is current. Windows are independent — closing one never disturbs
// another, and the process exits with the last.
Window {
    id: root

    property string initialPath: Platform.homePath()
    property string initialSelection: ""

    // A tab slot holds one pane, or two while split view (F3) is on. The
    // chrome acts on the slot's active pane, so `currentTab` stays the one
    // name everything below routes through.
    readonly property Item currentSlot: tabsRepeater.count > 0
                                        ? tabsRepeater.itemAt(stack.currentIndex) : null
    readonly property Item currentTab: currentSlot ? currentSlot.activePane : null
    readonly property bool splitOpen: currentSlot ? currentSlot.split : false
    readonly property int activePane: currentSlot ? currentSlot.activePaneIndex : 0

    // Read from C++ so a second launch can raise this window rather than
    // opening a duplicate of somewhere already on screen. The rest is the
    // window's observable state, which is what lets the UI be tested by
    // driving real keystrokes instead of trusting a screenshot.
    readonly property string currentPath: currentTab ? currentTab.path : ""
    readonly property int selectionCount: currentTab ? currentTab.selectionCount : 0
    readonly property string currentName: currentTab && currentTab.currentIndex >= 0
                                          ? currentTab.files.valueAt(currentTab.currentIndex, "name") : ""
    readonly property string viewMode: currentTab ? currentTab.viewMode : ""
    readonly property bool showHidden: currentTab ? currentTab.showHidden : false
    readonly property int sortKey: currentTab ? currentTab.sortKey : 0
    readonly property bool sortDescending: currentTab ? currentTab.sortDescending : false
    readonly property int zoom: currentTab ? currentTab.zoom : 0
    readonly property int visibleCount: currentTab ? currentTab.files.count : 0
    // The proxy's live value, not the Settings one: reading it end-to-end
    // proves the file → Settings → binding → proxy chain, which is what the
    // UI verification asserts on.
    readonly property bool foldersFirst: currentTab ? currentTab.files.foldersFirst : false
    // Same idea for list columns: the view's rendered set, joined for D-Bus.
    readonly property string listColumns: currentTab && currentTab.listColumns
                                          ? currentTab.listColumns.join(",") : ""
    readonly property string iconCaptions: currentTab && currentTab.iconCaptions
                                           ? currentTab.iconCaptions.join(",") : ""
    // The rendered Size cell of the current row — "12 items" for folders.
    readonly property string currentSizeCell: currentTab
                                              ? currentTab.currentSizeCell : ""
    // The current row's tree state — how verify-ui asserts expandable folders.
    readonly property int currentDepth: currentTab ? currentTab.currentDepth : 0
    readonly property bool currentExpanded: currentTab ? currentTab.currentExpanded : false
    // Live + queued operations, for the popover button and verify-ui.
    readonly property int operationsCount: FileOperations.operations.length
    readonly property int tabCount: tabModel.count

    // The sidebar: on by default, F9 toggles — Nautilus's binding.
    property bool sidebarVisible: true
    readonly property int placesCount: sidebar.placesCount

    // Search: Ctrl+F opens the bar, Escape closes it. Published for the UI
    // verification script like everything else it drives.
    property bool searchOpen: false
    readonly property string searchQuery: currentTab ? currentTab.searchQuery : ""
    readonly property bool searchContent: currentTab ? currentTab.searchContent : false
    // The filter popover's state, for WindowState.
    readonly property string searchDateRange: currentTab ? currentTab.searchDateRange : "any"
    readonly property string searchDateKind: currentTab ? currentTab.searchDateKind : "modified"
    readonly property string searchTypeFilter: currentTab ? currentTab.searchTypeFilter : "any"
    // The index only answers for local folders — elsewhere the toggle hides.
    readonly property bool searchContentAvailable: currentTab
        ? Platform.isLocal(currentTab.path) : false

    function openSearch() {
        searchOpen = true;
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function toggleSearchContent() {
        if (!currentTab || !searchContentAvailable)
            return;
        if (!searchOpen)
            openSearch();
        currentTab.searchContent = !currentTab.searchContent;
    }

    function closeSearch() {
        searchOpen = false;
        if (currentTab)
            currentTab.searchQuery = "";
        returnFocusToView();
    }

    // The properties dialog's own state, published for the same reason: it is
    // the only way to assert on a dialog without a human looking at it.
    readonly property bool preferencesOpen: preferencesDialog.opened
    readonly property bool propertiesOpen: propertiesDialog.opened
    readonly property string propertiesName: propertiesDialog.subjectName
    readonly property int propertiesCount: propertiesDialog.subjectCount
    // A string, not a number: a byte count travels over D-Bus as a double and
    // comes back out as "4321.0", which is a needlessly awkward thing to assert
    // on — and a lossy one once a file is bigger than 2^53.
    readonly property string propertiesSize: String(propertiesDialog.subjectSize)
    readonly property string propertiesMode: propertiesDialog.subjectMode
    readonly property int propertiesTab: propertiesDialog.currentTab

    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    color: Colors.window
    title: currentTab ? currentTab.title + " — Files" : "Files"

    // Quick Controls (menus, dialogs, fields, scrollbars) paint from the
    // palette. Without this they wear the style's stock grey and look like
    // a foreign toolkit dropped into an Omarchy window.
    palette {
        window: Colors.chrome
        windowText: Colors.text
        base: Colors.window
        alternateBase: Colors.chrome
        text: Colors.text
        button: Colors.chrome
        buttonText: Colors.text
        highlight: Colors.selection
        highlightedText: Colors.selectionText
        mid: Colors.border
        midlight: Colors.hover
        dark: Colors.border
        light: Colors.hover
        placeholderText: Colors.textDim
        toolTipBase: Colors.chrome
        toolTipText: Colors.text
        disabled {
            text: Colors.textDim
            buttonText: Colors.textDim
            windowText: Colors.textDim
        }
    }

    onClosing: App.windowClosed(root)

    // ---- tabs -------------------------------------------------------------

    ListModel { id: tabModel }

    function addTab(path, selection) {
        tabModel.append({ tabPath: path, tabSelection: selection || "" });
        stack.currentIndex = tabModel.count - 1;
    }

    function closeTab(index) {
        if (tabModel.count <= 1) {
            root.close();
            return;
        }
        tabModel.remove(index);
        stack.currentIndex = Math.min(index, tabModel.count - 1);
    }

    function cycleTab(delta) {
        if (tabModel.count < 2)
            return;
        stack.currentIndex = (stack.currentIndex + delta + tabModel.count) % tabModel.count;
    }

    Component.onCompleted: {
        addTab(root.initialPath, root.initialSelection);
        if (currentTab)
            currentTab.forceActiveFocus();
    }

    // ---- chrome -----------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Colors.chrome

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                // Nautilus 50's sidebar header: search, the app name and
                // the main menu sit over the sidebar column; with the sidebar
                // hidden (F9) the two buttons stay, compact, and the name goes.
                RowLayout {
                    spacing: 4
                    // A nested layout defaults to fillWidth: true — it would
                    // fight the path bar for every spare pixel.
                    Layout.fillWidth: false
                    Layout.preferredWidth: root.sidebarVisible ? 192 : -1

                    ToolbarButton {
                        symbol: "⌕"
                        tip: "Search (Ctrl+F)"
                        active: root.searchOpen
                        onTriggered: root.searchOpen ? root.closeSearch() : root.openSearch()
                    }

                    Text {
                        visible: root.sidebarVisible
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "Files"
                        color: Colors.text
                        font.pixelSize: 14
                        font.bold: true
                    }

                    ToolbarButton {
                        id: menuButton
                        symbol: "≡"
                        tip: "Main menu"
                        onTriggered: mainMenu.popup(menuButton, 0, menuButton.height)
                    }
                }

                ToolbarButton {
                    symbol: "←"
                    tip: "Back (Alt+Left)"
                    enabled: root.currentTab && root.currentTab.history.canGoBack
                    onTriggered: root.currentTab.goBack()
                }

                ToolbarButton {
                    symbol: "→"
                    tip: "Forward (Alt+Right)"
                    enabled: root.currentTab && root.currentTab.history.canGoForward
                    onTriggered: root.currentTab.goForward()
                }

                PathBar {
                    id: pathBar

                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: !root.searchOpen
                    path: root.currentTab ? root.currentTab.path : ""
                    onMenuRequested: pathBarMenu.popup()
                    onNavigateRequested: target => {
                        if (root.currentTab)
                            root.currentTab.navigate(target);
                        // Same rule as the dialogs: whoever took the keyboard
                        // gives it back. Without this, the first arrow key
                        // after a committed Ctrl+L lands in the path bar.
                        root.returnFocusToView();
                    }
                }

                // Search lives in the path bar's slot, as in Nautilus: no
                // extra row — the field replaces the breadcrumbs in place
                // while search is open, and the query still lives on the
                // tab so switching tabs shows that tab's search.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: root.searchOpen
                    radius: Colors.radius
                    color: Colors.window
                    border.color: searchField.activeFocus ? Colors.accent : Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 6

                        Text {
                            text: "⌕"
                            color: Colors.textDim
                            font.pixelSize: 15
                        }

                        TextField {
                            id: searchField

                            Layout.fillWidth: true
                            color: Colors.text
                            font.pixelSize: 13
                            background: null
                            placeholderText: qsTr("Search current folder")
                            selectByMouse: true
                            onTextEdited: {
                                if (root.currentTab)
                                    root.currentTab.searchQuery = text;
                            }
                            Keys.onEscapePressed: root.closeSearch()
                            Keys.onDownPressed: {
                                // Hand the keyboard to the results without closing.
                                root.returnFocusToView();
                                if (root.currentTab && root.currentTab.files.count > 0)
                                    root.currentTab.setCurrent(0, false);
                            }
                        }

                        Text {
                            visible: root.currentTab && root.currentTab.searching
                            text: qsTr("searching…")
                            color: Colors.accent
                            font.pixelSize: 11
                        }

                        // File-name vs full-text, Nautilus's search filter
                        // at the field's right edge. Hidden where the index
                        // can't answer (non-local).
                        Rectangle {
                            visible: root.searchContentAvailable
                            implicitWidth: contentsLabel.implicitWidth + 16
                            implicitHeight: 22
                            radius: 4
                            color: root.searchContent ? Colors.selection
                                 : contentsMouse.containsMouse ? Colors.hover : "transparent"
                            border.color: root.searchContent ? Colors.selection : Colors.border
                            border.width: 1

                            Text {
                                id: contentsLabel
                                anchors.centerIn: parent
                                text: qsTr("Contents")
                                color: root.searchContent ? Colors.selectionText : Colors.textDim
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: contentsMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleSearchContent()
                            }

                            ToolTip.visible: contentsMouse.containsMouse
                            ToolTip.text: qsTr("Search file contents (Ctrl+Shift+F)")
                            ToolTip.delay: 600
                        }

                        // Nautilus's search filters: a date window and a file
                        // type, in a popover off the field's edge. The chip
                        // stays lit while any filter narrows the results.
                        Rectangle {
                            readonly property bool filtersActive: root.currentTab
                                && (root.currentTab.searchDateRange !== "any"
                                    || root.currentTab.searchTypeFilter !== "any")

                            implicitWidth: filtersLabel.implicitWidth + 16
                            implicitHeight: 22
                            radius: 4
                            color: filtersActive ? Colors.selection
                                 : filtersMouse.containsMouse ? Colors.hover : "transparent"
                            border.color: filtersActive ? Colors.selection : Colors.border
                            border.width: 1

                            Text {
                                id: filtersLabel
                                anchors.centerIn: parent
                                text: qsTr("Filters")
                                color: parent.filtersActive ? Colors.selectionText : Colors.textDim
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: filtersMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterPopover.visible ? filterPopover.close()
                                                                 : filterPopover.open()
                            }

                            ToolTip.visible: filtersMouse.containsMouse && !filterPopover.visible
                            ToolTip.text: qsTr("Filter by date and file type")
                            ToolTip.delay: 600

                            Popup {
                                id: filterPopover

                                y: parent.height + 10
                                x: parent.width - width
                                width: 330
                                padding: 14
                                background: Rectangle {
                                    color: Colors.chrome
                                    border.color: Colors.border
                                    border.width: 1
                                    radius: Colors.radius
                                }

                                // Sampled, not bound: interacting with a
                                // ComboBox writes currentIndex, which would
                                // sever a binding on first use (sixth
                                // appearance of this pattern).
                                onAboutToShow: {
                                    const tab = root.currentTab;
                                    rangeCombo.currentIndex =
                                        Math.max(0, rangeValues.indexOf(tab.searchDateRange));
                                    kindCombo.currentIndex =
                                        Math.max(0, kindValues.indexOf(tab.searchDateKind));
                                    typeCombo.currentIndex =
                                        Math.max(0, typeValues.indexOf(tab.searchTypeFilter));
                                }

                                readonly property var rangeValues:
                                    ["any", "today", "yesterday", "week", "month", "year"]
                                readonly property var rangeLabels:
                                    [qsTr("Any time"), qsTr("Today"), qsTr("Since yesterday"),
                                     qsTr("Last 7 days"), qsTr("Last 30 days"), qsTr("Last year")]
                                readonly property var kindValues:
                                    ["modified", "created", "accessed"]
                                readonly property var kindLabels:
                                    [qsTr("Last Modified"), qsTr("Created"), qsTr("Last Used")]
                                readonly property var typeValues:
                                    ["any", "folders", "documents", "illustration", "music",
                                     "pdf", "pictures", "presentations", "spreadsheets",
                                     "text", "videos"]
                                readonly property var typeLabels:
                                    [qsTr("Anything"), qsTr("Folders"), qsTr("Documents"),
                                     qsTr("Illustration"), qsTr("Music"), qsTr("PDF / PostScript"),
                                     qsTr("Pictures"), qsTr("Presentations"), qsTr("Spreadsheets"),
                                     qsTr("Text Files"), qsTr("Videos")]

                                contentItem: Column {
                                    spacing: 10

                                    Text {
                                        text: qsTr("When")
                                        color: Colors.text
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    Row {
                                        spacing: 8

                                        ComboBox {
                                            id: rangeCombo
                                            width: 150
                                            model: filterPopover.rangeLabels
                                            onActivated: root.currentTab.searchDateRange =
                                                filterPopover.rangeValues[currentIndex]
                                        }

                                        ComboBox {
                                            id: kindCombo
                                            width: 132
                                            enabled: rangeCombo.currentIndex > 0
                                            model: filterPopover.kindLabels
                                            onActivated: root.currentTab.searchDateKind =
                                                filterPopover.kindValues[currentIndex]
                                        }
                                    }

                                    Text {
                                        text: qsTr("What")
                                        color: Colors.text
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    ComboBox {
                                        id: typeCombo
                                        width: 290
                                        model: filterPopover.typeLabels
                                        onActivated: root.currentTab.searchTypeFilter =
                                            filterPopover.typeValues[currentIndex]
                                    }
                                }
                            }
                        }
                    }
                }

                ToolbarButton {
                    // Shows the view you'd switch TO: four squares for grid,
                    // lines for list. "▦" was a crosshatch mess at 15px.
                    glyph: root.currentTab && root.currentTab.viewMode === "list" ? "view-grid" : ""
                    symbol: "☰"
                    tip: "Switch view (Ctrl+1 / Ctrl+2)"
                    onTriggered: {
                        if (root.currentTab)
                            root.setViewMode(root.currentTab.viewMode === "list" ? "icon" : "list");
                    }
                }

                ToolbarButton {
                    id: viewOptionsButton
                    symbol: "▼"
                    symbolSize: 12
                    tip: "View options"
                    onTriggered: viewOptionsMenu.popup(viewOptionsButton, 0, viewOptionsButton.height)
                }

                ToolbarButton {
                    symbol: "✕"
                    symbolSize: 13
                    tip: "Close window (Ctrl+Shift+W)"
                    onTriggered: root.close()
                }
            }
        }

        // Tab strip, hidden when there is only one tab — same as Nautilus.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 32 : 0
            visible: tabModel.count > 1
            color: Colors.chrome

            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                spacing: 2

                Repeater {
                    model: tabModel

                    delegate: Rectangle {
                        required property int index
                        required property string tabPath

                        width: Math.min(200, Math.max(120, root.width / tabModel.count - 8))
                        height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 4
                        color: index === stack.currentIndex ? Colors.window
                             : tabMouse.containsMouse ? Colors.hover : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: closeButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: Platform.baseName(tabPath) || "/"
                            color: index === stack.currentIndex ? Colors.text : Colors.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Text {
                            id: closeButton

                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "×"
                            color: closeMouse.containsMouse ? Colors.text : Colors.textDim
                            font.pixelSize: 14

                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                onClicked: root.closeTab(index)
                            }
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: mouse => {
                                if (mouse.button === Qt.MiddleButton)
                                    root.closeTab(index);
                                else
                                    stack.currentIndex = index;
                            }
                        }
                    }
                }
            }
        }

        // (The search field lives inline in the toolbar, in the path bar's
        // slot — see above. No separate search row, as in Nautilus.)

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar

                Layout.fillHeight: true
                Layout.preferredWidth: 200
                visible: root.sidebarVisible
                currentLocation: root.currentPath
                mounter: windowMounter
                onNavigateRequested: location => {
                    if (root.currentTab)
                        root.currentTab.navigate(location);
                }
                onMountError: (name, message) => root.flash(qsTr("Could not mount “%1”: %2").arg(name).arg(message))
                onOpsPopoverClosed: root.returnFocusToView()
                onDropRequested: (urls, location) => {
                    if (location === "trash:///")
                        FileOperations.trash(Platform.locationsFromUrls(urls));
                    else if (location === "starred:///")
                        StarredStore.star(Platform.locationsFromUrls(urls));
                    else if (root.currentTab)
                        root.currentTab.requestDrop(urls, location);
                }
                onOpenInNewTabRequested: location => root.addTab(location)
                onEmptyTrashRequested: emptyTrashConfirm.open()
            }

            StackLayout {
                id: stack

                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                onCurrentIndexChanged: {
                    if (root.currentTab)
                        root.currentTab.forceActiveFocus();
                }

                Repeater {
                    id: tabsRepeater
                    model: tabModel

                    delegate: TabPanes {
                        required property string tabPath
                        required property string tabSelection

                        initialPath: tabPath
                        initialSelection: tabSelection
                        onContextMenuRequested: contextMenu.popup()
                        onMountNeeded: location => windowMounter.mountLocation(location)
                        onTransferRequested: (sources, destination, isMove) =>
                            root.startTransfer(sources, destination, isMove, false)
                    }
                }
            }
        }

        // The server bar — Nautilus's "connect to server" surface, shown only
        // in the Network view. Connecting mounts first (credentials and all),
        // records the address as a known connection, then navigates into it.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            visible: root.viewingNetwork
            color: Colors.chrome

            Rectangle {
                width: parent.width
                height: 1
                color: Colors.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                TextField {
                    id: serverField

                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    Layout.preferredHeight: 30
                    color: Colors.text
                    font.pixelSize: 13
                    placeholderText: qsTr("Server address")
                    selectByMouse: true
                    onAccepted: root.connectToServer()
                }

                Button {
                    text: qsTr("Connect")
                    enabled: serverField.text.trim() !== ""
                    onClicked: root.connectToServer()
                }

                ToolbarButton {
                    id: protocolsButton

                    symbol: "ⓘ"
                    tip: "Available protocols"
                    onTriggered: protocolsPopover.opened ? protocolsPopover.close()
                                                        : protocolsPopover.open()

                    // Nautilus's Server Addresses help, honest by
                    // construction: only protocols gvfs here can actually
                    // mount are listed.
                    Popup {
                        id: protocolsPopover

                        x: parent.width - width
                        y: -height - 6
                        padding: 16

                        contentItem: Column {
                            spacing: 10

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Server Addresses")
                                color: Colors.text
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Text {
                                width: 320
                                text: qsTr("Server addresses are made up of a protocol prefix and an address. Examples:")
                                color: Colors.textDim
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: 320
                                text: "smb://gnome.org, ssh://192.168.0.1, ftp://[2001:db8::1]"
                                color: Colors.text
                                font.pixelSize: 12
                                wrapMode: Text.WrapAnywhere
                            }

                            GridLayout {
                                columns: 2
                                columnSpacing: 24
                                rowSpacing: 4

                                Text {
                                    text: qsTr("Available Protocols")
                                    color: Colors.text
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    text: qsTr("Prefix")
                                    color: Colors.text
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Repeater {
                                    model: root.protocolCells
                                    delegate: Text {
                                        required property var modelData
                                        text: modelData.text
                                        color: modelData.dim ? Colors.textDim : Colors.text
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Colors.chrome

            Rectangle {
                width: parent.width
                height: 1
                color: Colors.border
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                // While something is running, the status line belongs to it.
                text: FileOperations.busy ? FileOperations.statusText
                    : FileOperations.lastError !== "" ? FileOperations.lastError
                    : root.flashText !== "" ? root.flashText
                    : root.currentTab ? root.currentTab.statusText : ""
                color: !FileOperations.busy
                       && (FileOperations.lastError !== "" || root.flashText !== "")
                       ? Colors.error : Colors.textDim
                font.pixelSize: 11
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                visible: FileOperations.busy

                ProgressBar {
                    width: 160
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: 1
                    value: FileOperations.progress
                    indeterminate: FileOperations.progress <= 0
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cancel")
                    color: cancelMouse.containsMouse ? Colors.text : Colors.accent
                    font.pixelSize: 11

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        onClicked: FileOperations.cancel()
                    }
                }
            }

            // An error that nobody dismissed should not sit there forever.
            Timer {
                running: FileOperations.lastError !== "" && !FileOperations.busy
                interval: 8000
                onTriggered: FileOperations.clearError()
            }
        }
    }

    // The tab is the owner of the query; the field follows it — when tabs
    // switch, and when navigation clears the search.
    onSearchQueryChanged: {
        if (searchField.text !== searchQuery)
            searchField.text = searchQuery;
        if (searchQuery === "" && searchOpen && !searchField.activeFocus)
            searchOpen = false;
    }

    // A short-lived message on the status line, for events that have no
    // other surface — a failed mount, mainly.
    property string flashText: ""

    function flash(message) {
        flashText = message;
        flashTimer.restart();
    }

    Timer {
        id: flashTimer
        interval: 8000
        onTriggered: root.flashText = ""
    }

    // ---- file operations --------------------------------------------------

    function selection() { return currentTab ? currentTab.selectedPaths() : []; }

    // Switching views also persists the choice as the default for new tabs
    // and windows — Nautilus writes default-folder-viewer the same way.
    function setViewMode(mode) {
        if (!currentTab)
            return;
        currentTab.viewMode = mode;
        Settings.defaultViewMode = mode;
    }

    // A modal dialog takes the keyboard; nothing gives it back automatically.
    // Without this, the first New Folder leaves the window unnavigable.
    function returnFocusToView() {
        if (currentTab)
            currentTab.forceActiveFocus();
    }

    function newFolder() {
        newFolderPrompt.initialText = qsTr("New Folder");
        newFolderPrompt.ask();
    }

    function renameSelected() {
        const paths = selection();
        if (paths.length === 0)
            return;
        if (paths.length === 1) {
            renamePrompt.target = paths[0];
            renamePrompt.initialText = Platform.baseName(paths[0]);
            renamePrompt.ask();
        } else if (currentTab.batchRenamable) {
            batchRenameDialog.askAbout(currentTab.selectedItems(), currentTab.allNames());
        }
    }

    // The virtual roots make no sense as bookmarks; anything with a real
    // location — local or a mounted share — is fair game, as in Nautilus.
    readonly property bool folderBookmarkable: {
        if (!currentTab || currentTab.path === "")
            return false;
        const path = currentTab.path;
        return path !== "trash:///" && path !== "recent:///"
            && path !== "network:///" && path !== "starred:///";
    }

    function toggleBookmark() {
        if (folderBookmarkable)
            sidebar.toggleBookmark(currentTab.path);
    }

    // Rows in the trash cannot be trashed again — there, the delete key means
    // what Nautilus makes it mean: permanent delete, behind the confirm.
    readonly property bool viewingTrash: currentTab ? currentTab.path === "trash:///" : false

    // The Network view carries its own chrome: the server bar below the view
    // and Forget Connection in the context menu.
    readonly property bool viewingNetwork: currentTab ? currentTab.viewingNetwork : false
    readonly property bool serverBarVisible: viewingNetwork

    // The protocol help's table cells (label, prefix, label, prefix…),
    // filtered to what gvfs on this machine can actually mount.
    readonly property var protocolCells: {
        const schemes = Platform.supportedSchemes();
        const known = [
            { scheme: "afp", label: qsTr("AppleTalk"), prefix: "afp://" },
            { scheme: "ftp", label: qsTr("File Transfer Protocol"),
              prefix: schemes.indexOf("ftps") >= 0 ? "ftp:// or ftps://" : "ftp://" },
            { scheme: "nfs", label: qsTr("Network File System"), prefix: "nfs://" },
            { scheme: "smb", label: qsTr("Samba"), prefix: "smb://" },
            { scheme: "sftp", label: qsTr("SSH File Transfer Protocol"),
              prefix: schemes.indexOf("ssh") >= 0 ? "sftp:// or ssh://" : "sftp://" },
            { scheme: "dav", label: qsTr("WebDAV"),
              prefix: schemes.indexOf("davs") >= 0 ? "dav:// or davs://" : "dav://" },
        ];
        const cells = [];
        for (const p of known) {
            if (schemes.indexOf(p.scheme) < 0)
                continue;
            cells.push({ text: p.label, dim: false });
            cells.push({ text: p.prefix, dim: true });
        }
        return cells;
    }

    // The address being connected to through the server bar. Only a mount
    // this window asked for lands in the known-connections store — a sidebar
    // click on an unmounted volume must not.
    property string pendingServer: ""

    function connectToServer() {
        if (!currentTab)
            return;
        const address = serverField.text.trim();
        if (address === "")
            return;
        if (address.indexOf("://") < 0) {
            flash(qsTr("Addresses need a protocol prefix — smb://, sftp://, ftp://…"));
            return;
        }
        pendingServer = address;
        windowMounter.mountLocation(address);
    }

    function compressSelected() {
        const paths = selection();
        if (paths.length === 0 || !currentTab)
            return;
        // A single item suggests its own name, as Nautilus does.
        const suggested = paths.length === 1
            ? Platform.archiveStem(Platform.baseName(paths[0])) : "";
        compressDialog.askAbout(paths, currentTab.path, suggested, currentTab.allNames());
    }

    function extractSelected() {
        const paths = selection();
        if (paths.length > 0 && currentTab)
            FileOperations.extractHere(paths, currentTab.path);
    }

    // "Extract to…" — same operation, the destination picked first.
    function extractSelectedTo() {
        const paths = selection();
        if (paths.length === 0 || !currentTab)
            return;
        extractPicker.pendingArchives = paths;
        const what = paths.length === 1
            ? "\u201c" + Platform.baseName(paths[0]) + "\u201d"
            : qsTr("%1 archives").arg(paths.length);
        extractPicker.askFor(currentTab.path, qsTr("Extract %1 to:").arg(what));
    }

    function trashSelected() {
        if (viewingTrash) {
            deleteSelected();
            return;
        }
        const paths = selection();
        if (paths.length > 0)
            FileOperations.trash(paths);
    }

    function restoreSelected() {
        if (!currentTab)
            return;
        const originals = currentTab.selectedOrigPaths();
        if (originals.length > 0)
            FileOperations.restoreFromTrash(originals);
    }

    function deleteSelected() {
        const paths = selection();
        if (paths.length === 0)
            return;
        deleteConfirm.message = paths.length === 1
            ? qsTr("Permanently delete “%1”?").arg(Platform.baseName(paths[0]))
            : qsTr("Permanently delete %1 items?").arg(paths.length);
        deleteConfirm.detail = qsTr("This cannot be undone.");
        deleteConfirm.pending = paths;
        deleteConfirm.open();
    }

    function paste() {
        if (!currentTab)
            return;
        startTransfer(Clipboard.paths(), currentTab.path, Clipboard.isCut(), true);
    }

    // One flow for everything that lands files somewhere: paste and drops
    // both check for name clashes up front and share the conflict dialog.
    property var pendingTransfer: null

    function startTransfer(paths, destination, isMove, fromCut) {
        if (paths.length === 0 || !destination)
            return;
        pendingTransfer = { paths: paths, destination: destination,
                            isMove: isMove, fromCut: fromCut === true };
        const clashes = Platform.collisions(paths, destination);
        if (clashes.length > 0)
            conflictDialog.askAbout(clashes);
        else
            performTransfer(FileOperations.RenameNew);
    }

    function performTransfer(policy) {
        const transfer = pendingTransfer;
        pendingTransfer = null;
        if (!transfer)
            return;
        if (transfer.isMove)
            FileOperations.move(transfer.paths, transfer.destination, policy);
        else
            FileOperations.copy(transfer.paths, transfer.destination, policy);
        if (transfer.fromCut)
            Clipboard.clear();
    }

    PromptDialog {
        id: newFolderPrompt
        onClosed: root.returnFocusToView()
        prompt: qsTr("Name for the new folder")
        onAccepted_: name => FileOperations.createFolder(root.currentTab.path, name)
    }

    PromptDialog {
        id: renamePrompt
        onClosed: root.returnFocusToView()
        property string target: ""
        prompt: qsTr("New name")
        selectStem: true
        onAccepted_: name => FileOperations.rename(target, name)
    }

    ConfirmDialog {
        id: deleteConfirm
        onClosed: root.returnFocusToView()
        property var pending: []
        confirmText: qsTr("Delete")
        onConfirmed: FileOperations.deletePermanently(pending)
    }

    ConfirmDialog {
        id: emptyTrashConfirm
        onClosed: root.returnFocusToView()
        message: qsTr("Empty the trash?")
        detail: qsTr("Everything in the trash will be permanently deleted. This cannot be undone.")
        confirmText: qsTr("Empty Trash")
        onConfirmed: FileOperations.emptyTrash()
    }

    ConflictDialog {
        id: conflictDialog
        onClosed: root.returnFocusToView()
        onChosen: policy => root.performTransfer(policy)
    }

    PropertiesDialog {
        id: propertiesDialog
        onClosed: root.returnFocusToView()
    }

    BatchRenameDialog {
        id: batchRenameDialog
        onClosed: root.returnFocusToView()
    }

    CompressDialog {
        id: compressDialog
        onClosed: root.returnFocusToView()
    }

    FolderPickerDialog {
        id: extractPicker

        property var pendingArchives: []

        acceptLabel: qsTr("Extract")
        onPicked: path => FileOperations.extractHere(pendingArchives, path)
        onClosed: root.returnFocusToView()
    }

    // An extract hit an encrypted archive: ask, then replay with the answer.
    PromptDialog {
        id: passphraseDialog

        secret: true
        onAccepted_: text => FileOperations.providePassphrase(text)
        onRejected: FileOperations.declinePassphrase()
        onClosed: root.returnFocusToView()
    }

    Connections {
        target: FileOperations
        function onPassphraseNeeded(archiveName) {
            passphraseDialog.prompt =
                qsTr("\u201c%1\u201d is password-protected. Enter the password:")
                    .arg(archiveName);
            passphraseDialog.initialText = "";
            passphraseDialog.ask();
        }
    }

    // ---- mounting ---------------------------------------------------------

    Mounter {
        id: windowMounter

        // The tab that asked is reloaded once its filesystem is there. Only
        // the matching tab: the user may have moved on meanwhile.
        onMounted: location => {
            if (location === root.pendingServer) {
                // A server-bar connect: remember the address and go there.
                ServerStore.add(location);
                root.pendingServer = "";
                serverField.clear();
                if (root.currentTab)
                    root.currentTab.navigate(location);
                return;
            }
            if (root.currentTab && root.currentTab.path === location)
                root.currentTab.reload();
        }
        onMountFailed: (location, message) => {
            if (location === root.pendingServer)
                root.pendingServer = "";
            if (message !== "") {
                root.flash(qsTr("Could not mount: %1").arg(message));
            } else if (root.currentTab && root.currentTab.path === location) {
                // Cancelled from the dialog — leave the place, don't re-ask.
                root.currentTab.goBack();
            }
        }
        onAskPassword: (message, defaultUser, defaultDomain, needsUsername, needsDomain, needsPassword, canAnonymous) => {
            credentialDialog.ask(message, defaultUser, defaultDomain,
                                 needsUsername, needsDomain, needsPassword, canAnonymous);
        }
    }

    CredentialDialog {
        id: credentialDialog
        onClosed: root.returnFocusToView()
        onAnswered: (username, domain, password, anonymous, remember) =>
            windowMounter.providePassword(username, domain, password, anonymous, remember)
        onDismissed: windowMounter.cancelPassword()
    }

    // Properties of the selection, or of the folder being viewed when nothing
    // is selected — which is how you ask "how big is this folder?".
    function showProperties() {
        if (!currentTab)
            return;
        const paths = selection();
        propertiesDialog.show(paths.length > 0 ? paths : [currentTab.path]);
    }

    // Called from C++ for FileManager1's ShowItemProperties, so another
    // application's "Properties" button lands on the real dialog.
    function showPropertiesFor(paths) {
        if (paths.length > 0)
            propertiesDialog.show(paths);
    }

    // Trash does not exist on every filesystem — tmpfs has none. Rather than
    // leaving the user with a bare error, offer the thing they actually meant.
    ConfirmDialog {
        id: trashUnavailable
        onClosed: root.returnFocusToView()
        property var pending: []
        message: qsTr("These files can't be moved to the trash.")
        detail: qsTr("This location has no trash. Delete them permanently instead?")
        confirmText: qsTr("Delete permanently")
        onConfirmed: FileOperations.deletePermanently(pending)
    }

    Connections {
        target: FileOperations
        function onLastErrorChanged() {
            const message = FileOperations.lastError;
            if (message === "")
                return;
            if (message.indexOf("not supported") >= 0 && root.selection().length > 0) {
                trashUnavailable.pending = root.selection();
                trashUnavailable.open();
                FileOperations.clearError();
            }
        }
    }

    // ---- main menu (the hamburger) ----------------------------------------

    Menu {
        id: mainMenu

        MenuItem {
            text: qsTr("New Window")
            onTriggered: App.openWindow(root.currentTab ? root.currentTab.path : Platform.homePath())
        }

        MenuItem {
            text: qsTr("New Tab")
            onTriggered: root.addTab(root.currentTab ? root.currentTab.path : Platform.homePath())
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Undo")
            enabled: FileOperations.canUndo
            onTriggered: FileOperations.undo()
        }

        MenuItem {
            text: qsTr("Redo")
            enabled: FileOperations.canRedo
            onTriggered: FileOperations.redo()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Preferences")
            onTriggered: preferencesDialog.open()
        }

        MenuItem {
            text: qsTr("Keyboard Shortcuts")
            onTriggered: shortcutsDialog.open()
        }

        MenuItem {
            text: qsTr("About Files")
            onTriggered: aboutDialog.open()
        }
    }

    PreferencesDialog {
        id: preferencesDialog
        onClosed: root.returnFocusToView()
    }

    ShortcutsDialog {
        id: shortcutsDialog
        onClosed: root.returnFocusToView()
    }

    AboutDialog {
        id: aboutDialog
        onClosed: root.returnFocusToView()
    }

    VisibleColumnsDialog {
        id: visibleColumnsDialog
        onClosed: root.returnFocusToView()
    }

    // ---- view options menu ------------------------------------------------

    // Nautilus's view-options popover (the ▾ next to the view toggle): icon
    // size, the sort orders, hidden files. Sort state lives on the tab, so
    // this menu and the list view's column headers can never disagree.
    Menu {
        id: viewOptionsMenu

        // Sampled when the menu opens rather than bound: triggering a
        // checkable MenuItem writes `checked`, which would sever a binding
        // the first time any item was clicked.
        onAboutToShow: {
            const key = root.currentTab ? root.currentTab.sortKey : FileSortFilterModel.ByName;
            const desc = root.currentTab ? root.currentTab.sortDescending : false;
            sortAZ.checked = key === FileSortFilterModel.ByName && !desc;
            sortZA.checked = key === FileSortFilterModel.ByName && desc;
            sortNewest.checked = key === FileSortFilterModel.ByModified && desc;
            sortOldest.checked = key === FileSortFilterModel.ByModified && !desc;
            sortLargest.checked = key === FileSortFilterModel.BySize && desc;
            sortByType.checked = key === FileSortFilterModel.ByType && !desc;
            hiddenToggle.checked = root.showHidden;
        }

        function setSort(key, descending) {
            if (!root.currentTab)
                return;
            root.currentTab.sortKey = key;
            root.currentTab.sortDescending = descending;
        }

        // The zoom stepper, only for the view the zoom actually drives.
        // A hidden item still occupies its row, so the height collapses too.
        Item {
            visible: root.viewMode === "icon"
            implicitWidth: 220
            implicitHeight: visible ? 36 : 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 6
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Icon Size")
                    color: Colors.text
                    font.pixelSize: 13
                }

                ToolbarButton {
                    symbol: "−"
                    tip: "Zoom out (Ctrl+-)"
                    enabled: root.currentTab && root.currentTab.zoom > 32
                    onTriggered: root.currentTab.setZoom(root.currentTab.zoom - 16)
                }

                ToolbarButton {
                    symbol: "+"
                    tip: "Zoom in (Ctrl++)"
                    enabled: root.currentTab && root.currentTab.zoom < 128
                    onTriggered: root.currentTab.setZoom(root.currentTab.zoom + 16)
                }
            }
        }

        MenuSeparator {
            visible: root.viewMode === "icon"
            height: visible ? implicitHeight : 0
        }

        MenuItem { text: qsTr("Sort"); enabled: false }

        MenuItem {
            id: sortAZ
            text: qsTr("A-Z")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByName, false)
        }

        MenuItem {
            id: sortZA
            text: qsTr("Z-A")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByName, true)
        }

        MenuItem {
            id: sortNewest
            text: qsTr("Last Modified")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByModified, true)
        }

        MenuItem {
            id: sortOldest
            text: qsTr("First Modified")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByModified, false)
        }

        MenuItem {
            id: sortLargest
            text: qsTr("Size")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.BySize, true)
        }

        MenuItem {
            id: sortByType
            text: qsTr("Type")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByType, false)
        }

        MenuSeparator {
            visible: root.viewMode === "list"
            height: visible ? implicitHeight : 0
        }

        MenuItem {
            text: qsTr("Visible Columns…")
            visible: root.viewMode === "list"
            height: visible ? implicitHeight : 0
            onTriggered: visibleColumnsDialog.open()
        }

        MenuSeparator {}

        MenuItem {
            id: hiddenToggle
            text: qsTr("Show Hidden Files")
            checkable: true
            onTriggered: {
                if (root.currentTab)
                    root.currentTab.showHidden = !root.currentTab.showHidden;
            }
        }
    }

    // ---- path-bar menu ----------------------------------------------------

    // The pill's kebab: Nautilus 50's current-folder menu. Folder-scoped —
    // never about the selection, whatever is selected.
    Menu {
        id: pathBarMenu

        property var templateFiles: []
        property bool newDocumentShown: false

        onAboutToShow: {
            // Same trick as contextMenu: a submenu can't play the height:0
            // trick, so New Document is inserted/removed on each open.
            templateFiles = Platform.templates();
            const wantNewDocument = templateFiles.length > 0 && root.currentTab
                && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path);
            if (wantNewDocument !== newDocumentShown) {
                if (wantNewDocument)
                    pathBarMenu.insertMenu(1, pathBarNewDocumentMenu);
                else
                    pathBarMenu.removeMenu(pathBarNewDocumentMenu);
                newDocumentShown = wantNewDocument;
            }
        }

        MenuItem {
            text: qsTr("New Folder…")
            enabled: root.currentTab !== null
            onTriggered: root.newFolder()
        }

        MenuItem {
            text: qsTr("Open With…")
            enabled: root.currentTab !== null
            onTriggered: {
                propertiesDialog.show([root.currentTab.path]);
                propertiesDialog.selectTab(2);
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Reload")
            enabled: root.currentTab !== null
            onTriggered: root.currentTab.reload()
        }

        MenuItem {
            text: qsTr("Copy Location")
            enabled: root.currentTab !== null
            onTriggered: Clipboard.copyText(root.currentTab.path)
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Paste")
            enabled: Clipboard.hasFiles && root.currentTab !== null
            onTriggered: root.paste()
        }

        MenuItem {
            text: qsTr("Paste as Link")
            enabled: Clipboard.hasFiles && root.currentTab !== null
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: FileOperations.createLink(Clipboard.paths(), root.currentTab.path)
        }

        MenuItem {
            text: qsTr("Select All")
            enabled: root.currentTab !== null
            onTriggered: root.currentTab.selectAll()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Properties")
            enabled: root.currentTab !== null
            onTriggered: propertiesDialog.show([root.currentTab.path])
        }
    }

    // Its New Document submenu — same template list and transfer flow as the
    // context menu's, but owned here: a Menu has one parent at a time.
    Menu {
        id: pathBarNewDocumentMenu
        title: qsTr("New Document")

        Instantiator {
            model: pathBarMenu.templateFiles
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: root.startTransfer([modelData.path],
                                                root.currentTab.path, false, false)
            }
            onObjectAdded: (index, object) => pathBarNewDocumentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => pathBarNewDocumentMenu.removeItem(object)
        }
    }

    // ---- context menu -----------------------------------------------------

    Menu {
        id: contextMenu

        // Sampled when the menu opens: the bookmarks file has no notify
        // signal a binding could follow — and the user actions depend on
        // what is selected right now.
        property bool folderBookmarked: false
        property bool selectionStarred: false
        property bool selectionForgettable: false
        property bool selectionExtractable: false
        property var selectionActions: []
        property var actionPaths: []
        property var templateFiles: []
        property bool newDocumentShown: false
        onAboutToShow: {
            folderBookmarked = root.currentTab
                ? sidebar.isBookmarked(root.currentTab.path) : false;
            actionPaths = root.selection();
            selectionStarred = StarredStore.allStarred(actionPaths);
            selectionForgettable = root.viewingNetwork && ServerStore.allKnown(actionPaths);
            selectionExtractable = root.currentTab && root.currentTab.batchRenamable
                && Platform.isLocal(root.currentTab.path)
                && root.currentTab.selectionAllArchives();
            selectionActions = UserActions.actionsFor(actionPaths);

            // New Document exists only while ~/Templates has files and the
            // view is a real local directory — hidden otherwise, as
            // Nautilus. A submenu can't play the height:0 trick, so it is
            // inserted after New Folder / removed on each open.
            templateFiles = Platform.templates();
            const wantNewDocument = templateFiles.length > 0 && root.currentTab
                && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path);
            if (wantNewDocument !== newDocumentShown) {
                if (wantNewDocument) {
                    let at = contextMenu.count;
                    for (let i = 0; i < contextMenu.count; ++i) {
                        const item = contextMenu.itemAt(i);
                        if (item && item.text === qsTr("New Folder")) { at = i + 1; break; }
                    }
                    contextMenu.insertMenu(at, newDocumentMenu);
                } else {
                    contextMenu.removeMenu(newDocumentMenu);
                }
                newDocumentShown = wantNewDocument;
            }
        }

        MenuItem {
            text: "Open"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.currentTab.activate(root.currentTab.currentIndex)
        }

        MenuSeparator {}

        MenuItem {
            text: "Open in Terminal"
            // A terminal needs a working directory; trash:// has none.
            enabled: root.currentTab && Platform.isLocal(root.currentTab.path)
            onTriggered: {
                if (!root.currentTab)
                    return;
                // A selected folder is the more useful target; otherwise the
                // folder being viewed.
                const selected = root.currentTab.selectedPaths();
                const target = selected.length === 1 && Platform.isDir(selected[0])
                             ? selected[0] : root.currentTab.path;
                Platform.openTerminal(target);
            }
        }

        MenuItem {
            text: "Open in New Tab"
            enabled: root.currentTab && root.currentTab.selectionCount === 1
            onTriggered: {
                const selected = root.currentTab.selectedPaths();
                if (selected.length === 1 && Platform.isDir(selected[0]))
                    root.addTab(selected[0]);
            }
        }

        MenuItem {
            text: contextMenu.folderBookmarked ? qsTr("Remove Bookmark") : qsTr("Bookmark This Folder")
            enabled: root.folderBookmarkable
            onTriggered: root.toggleBookmark()
        }

        MenuItem {
            // Nautilus's star toggle: Star when anything in the selection is
            // unstarred, Unstar only when the whole selection is starred.
            text: contextMenu.selectionStarred ? qsTr("Unstar") : qsTr("Star")
            // Local folders and the Starred view itself (paths there are the
            // real files); trash rows have nothing to pin.
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && (Platform.isLocal(root.currentTab.path)
                         || root.currentTab.path === "starred:///")
            onTriggered: {
                if (contextMenu.selectionStarred)
                    StarredStore.unstar(contextMenu.actionPaths);
                else
                    StarredStore.star(contextMenu.actionPaths);
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Cut")
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: Clipboard.cutFiles(root.selection())
        }

        MenuItem {
            text: qsTr("Copy")
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: Clipboard.copyFiles(root.selection())
        }

        MenuItem {
            text: qsTr("Paste")
            enabled: Clipboard.hasFiles
            onTriggered: root.paste()
        }

        MenuItem {
            // Nautilus's optional Create Link action — hidden until the
            // preference turns it on.
            text: qsTr("Create Link")
            visible: Settings.showCreateLink
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: FileOperations.createLink(root.selection(), root.currentTab.path)
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("New Folder")
            onTriggered: root.newFolder()
        }

        MenuItem {
            text: qsTr("Rename…")
            // One item renames inline; several open batch rename, which
            // needs a real directory under it.
            enabled: root.currentTab && (root.currentTab.selectionCount === 1
                     || (root.currentTab.selectionCount > 1 && root.currentTab.batchRenamable))
            onTriggered: root.renameSelected()
        }

        MenuItem {
            text: qsTr("Compress…")
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: root.compressSelected()
        }

        MenuItem {
            text: qsTr("Extract Here")
            visible: contextMenu.selectionExtractable
            height: visible ? implicitHeight : 0
            onTriggered: root.extractSelected()
        }

        MenuItem {
            text: qsTr("Extract to…")
            visible: contextMenu.selectionExtractable
            height: visible ? implicitHeight : 0
            onTriggered: root.extractSelectedTo()
        }

        MenuItem {
            // Nautilus's label for dropping a server from the known list.
            // Only for remembered servers — a discovered or mounted row has
            // nothing here to forget.
            text: qsTr("Forget Connection")
            visible: root.viewingNetwork
            height: visible ? implicitHeight : 0
            enabled: contextMenu.selectionForgettable
            onTriggered: ServerStore.remove(contextMenu.actionPaths)
        }

        MenuItem {
            text: qsTr("Restore from Trash")
            visible: root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.restoreSelected()
        }

        MenuItem {
            text: qsTr("Empty Trash…")
            visible: root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.visibleCount > 0
            onTriggered: emptyTrashConfirm.open()
        }

        MenuItem {
            text: qsTr("Move to Trash")
            visible: !root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.trashSelected()
        }

        MenuItem {
            text: qsTr("Delete Permanently…")
            // Nautilus hides this behind a preference — except in the trash,
            // where permanent delete is the only kind there is. Shift+Delete
            // works regardless, as the preference dialog says.
            visible: root.viewingTrash || Settings.showDeletePermanently
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.deleteSelected()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Properties")
            onTriggered: root.showProperties()
        }

        MenuSeparator {}

        MenuItem {
            text: root.currentTab && root.currentTab.showHidden ? "Hide Hidden Files" : "Show Hidden Files"
            onTriggered: root.currentTab.showHidden = !root.currentTab.showHidden
        }

        MenuItem {
            text: "Reload"
            onTriggered: root.currentTab.reload()
        }

        MenuSeparator {
            visible: contextMenu.selectionActions.length > 0
            height: visible ? implicitHeight : 0
        }

        // The user actions (transcode, omarchy-send, …) — declarative TOML
        // files, the replacement for the nautilus-python extensions.
        // Instantiated fresh from whatever aboutToShow computed; addItem
        // appends after the static entries above.
        Instantiator {
            model: contextMenu.selectionActions
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: UserActions.run(modelData.id, contextMenu.actionPaths)
            }
            onObjectAdded: (index, object) => contextMenu.addItem(object)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object)
        }
    }

    // The New Document submenu — one item per file in ~/Templates, copied
    // into the folder being viewed through the shared transfer flow (so a
    // name clash gets the conflict dialog and the copy is one Ctrl+Z).
    // Declared outside contextMenu: the parent menu inserts and removes it
    // in onAboutToShow, because Nautilus hides the entry entirely when
    // there are no templates.
    Menu {
        id: newDocumentMenu
        title: qsTr("New Document")

        Instantiator {
            model: contextMenu.templateFiles
            MenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: root.startTransfer([modelData.path],
                                                root.currentTab.path, false, false)
            }
            onObjectAdded: (index, object) => newDocumentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => newDocumentMenu.removeItem(object)
        }
    }

    // ---- mouse back/forward ----------------------------------------------

    // Buttons 8 and 9 on a mouse are back and forward everywhere else on the
    // desktop; a file manager that ignores them feels broken.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        z: 100
        onPressed: mouse => {
            if (!root.currentTab) {
                mouse.accepted = false;
                return;
            }
            if (mouse.button === Qt.BackButton)
                root.currentTab.goBack();
            else if (mouse.button === Qt.ForwardButton)
                root.currentTab.goForward();
        }
    }

    // ---- shortcuts --------------------------------------------------------

    Shortcut { sequence: "Ctrl+T"; onActivated: root.addTab(root.currentTab ? root.currentTab.path : Platform.homePath()) }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.closeTab(stack.currentIndex) }
    Shortcut { sequence: "Ctrl+N"; onActivated: App.openWindow(root.currentTab ? root.currentTab.path : Platform.homePath()) }
    Shortcut { sequence: "Ctrl+Shift+W"; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: root.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: root.cycleTab(-1) }
    Shortcut { sequence: "Ctrl+PgDown"; onActivated: root.cycleTab(1) }
    Shortcut { sequence: "Ctrl+PgUp"; onActivated: root.cycleTab(-1) }

    Shortcut { sequence: "Alt+Left"; onActivated: if (root.currentTab) root.currentTab.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: if (root.currentTab) root.currentTab.goForward() }
    Shortcut { sequence: "Alt+Up"; onActivated: if (root.currentTab) root.currentTab.goUp() }
    Shortcut { sequence: "Alt+Home"; onActivated: if (root.currentTab) root.currentTab.navigate(Platform.homePath()) }

    // Split view: F3 toggles the second pane, F6 moves the keyboard (and the
    // chrome) between panes — the classic dual-pane bindings.
    Shortcut { sequence: "F3"; onActivated: if (root.currentSlot) root.currentSlot.toggleSplit() }
    Shortcut { sequence: "F6"; onActivated: if (root.currentSlot) root.currentSlot.cyclePane() }

    Shortcut { sequence: "F9"; onActivated: root.sidebarVisible = !root.sidebarVisible }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.toggleBookmark() }
    Shortcut { sequence: "Ctrl+F"; onActivated: root.openSearch() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: root.toggleSearchContent() }
    Shortcut { sequence: "Ctrl+L"; onActivated: pathBar.beginEditing() }
    Shortcut { sequence: "Ctrl+H"; onActivated: if (root.currentTab) root.currentTab.showHidden = !root.currentTab.showHidden }
    Shortcut { sequence: "Ctrl+A"; onActivated: if (root.currentTab) root.currentTab.selectAll() }
    Shortcut { sequence: "Ctrl+R"; onActivated: if (root.currentTab) root.currentTab.reload() }
    Shortcut { sequence: "F5"; onActivated: if (root.currentTab) root.currentTab.reload() }

    Shortcut { sequence: "Delete"; onActivated: root.trashSelected() }
    Shortcut { sequence: "Shift+Delete"; onActivated: root.deleteSelected() }
    Shortcut { sequence: "F2"; onActivated: root.renameSelected() }
    Shortcut { sequence: "Ctrl+C"; onActivated: Clipboard.copyFiles(root.selection()) }
    Shortcut { sequence: "Ctrl+X"; onActivated: Clipboard.cutFiles(root.selection()) }
    Shortcut { sequence: "Ctrl+V"; onActivated: root.paste() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: FileOperations.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: FileOperations.redo() }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: root.newFolder() }

    // Ctrl+I is Nautilus's; Alt+Return is what the rest of the desktop uses.
    Shortcut { sequence: "Ctrl+I"; onActivated: root.showProperties() }
    Shortcut { sequence: "Alt+Return"; onActivated: root.showProperties() }

    Shortcut { sequence: "Ctrl+,"; onActivated: preferencesDialog.open() }
    Shortcut { sequence: "Ctrl+?"; onActivated: shortcutsDialog.open() }

    Shortcut { sequence: "Ctrl+1"; onActivated: root.setViewMode("list") }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.setViewMode("icon") }
    Shortcut { sequence: "Ctrl++"; onActivated: if (root.currentTab) root.currentTab.setZoom(root.currentTab.zoom + 16) }
    Shortcut { sequence: "Ctrl+="; onActivated: if (root.currentTab) root.currentTab.setZoom(root.currentTab.zoom + 16) }
    Shortcut { sequence: "Ctrl+-"; onActivated: if (root.currentTab) root.currentTab.setZoom(root.currentTab.zoom - 16) }
    Shortcut { sequence: "Ctrl+0"; onActivated: if (root.currentTab) root.currentTab.setZoom(64) }
}
