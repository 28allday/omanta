import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// The file chooser: what an app's Open, Save and Select Folder dialogs show
// when omanta is the desktop's file picker (`omanta --portal`, see
// FileChooserPortal). Built from the browser's own parts — sidebar, path bar,
// tab and views — so picking a file looks and behaves like browsing for one.
//
// `request` comes from the portal call (mode, title, the app's name, file
// type filters, extra choices, where to start); `finished` goes back exactly
// once, whichever way the window ends.
Window {
    id: root

    property var request: ({})

    signal finished(bool accepted, var paths, int filterIndex, var choiceValues)

    // "open" (one file, several, or a folder), "save" (one file, named here)
    // or "saveFiles" (a folder to drop several named files into).
    readonly property string mode: request.mode || "open"
    readonly property bool saving: mode === "save"
    readonly property bool savingMany: mode === "saveFiles"
    readonly property bool choosingFolder: savingMany || (mode === "open" && request.directory === true)
    readonly property bool openingFiles: mode === "open" && !choosingFolder
    readonly property bool multiple: openingFiles && request.multiple === true
    readonly property var filters: request.filters || []
    readonly property var choices: request.choices || []
    readonly property string appName: request.appName || ""

    property int filterIndex: request.filterIndex !== undefined ? request.filterIndex : -1
    // Choice id → chosen value, seeded from the request.
    property var choiceState: ({})
    property bool answered: false
    property bool showPreview: true
    property bool searchOpen: false

    readonly property string folder: pane.path
    // Recent, Starred, Network and Trash list things that live elsewhere; a
    // file can be picked from them, but nothing can be saved "into" them.
    readonly property bool folderIsReal: !/^(recent|starred|network|trash):/.test(folder)

    readonly property string heading: request.title
        || (saving ? qsTr("Save File")
            : savingMany ? qsTr("Save Files")
            : choosingFolder ? qsTr("Select Folder")
            : multiple ? qsTr("Open Files") : qsTr("Open File"))
    readonly property string caption: appName === "" ? ""
        : saving ? qsTr("%1 is saving a file").arg(appName)
        : savingMany ? qsTr("%1 is saving files").arg(appName)
        : choosingFolder ? qsTr("%1 wants a folder").arg(appName)
        : multiple ? qsTr("%1 wants files").arg(appName)
        : qsTr("%1 wants a file").arg(appName)

    // The selected rows, with what the buttons and the preview need to know.
    // Refreshed on a change rather than bound: walking the listing inside a
    // binding while it is still filling in is a binding loop.
    property var selectedRows: []

    function refreshSelection() {
        const names = pane.selectedNames;
        const files = pane.files;
        const rows = [];
        for (let row = 0; files && row < files.count; ++row) {
            const name = files.valueAt(row, "name");
            if (names[name] !== true)
                continue;
            const displayName = files.valueAt(row, "displayName");
            const contentType = files.valueAt(row, "contentType");
            const isDir = files.valueAt(row, "isDir") === true;
            rows.push({
                path: pane.actionPathAt(row),
                name: displayName,
                isDir: isDir,
                contentType: contentType,
                matches: pickerFilter.matches(displayName, contentType, isDir),
                size: files.valueAt(row, "size"),
                modified: files.valueAt(row, "modified"),
                typeDescription: files.valueAt(row, "typeDescription"),
                iconSource: files.valueAt(row, "iconSource")
            });
        }
        selectedRows = rows;
    }

    Connections {
        target: pane
        function onSelectedNamesChanged() { Qt.callLater(root.refreshSelection); }
    }

    Connections {
        target: pickerFilter
        function onRulesChanged() { Qt.callLater(root.refreshSelection); }
    }

    readonly property var pickedFiles: selectedRows.filter(r => !r.isDir && r.matches)
    readonly property var selectedFolder: selectedRows.length === 1 && selectedRows[0].isDir
                                          ? selectedRows[0] : null

    // The folder a folder-choice ends on: the one selected, else the one open.
    readonly property string chosenFolder: selectedFolder ? selectedFolder.path : folder

    readonly property bool acceptEnabled: {
        if (saving)
            return folderIsReal && nameField.text.trim() !== "";
        if (choosingFolder)
            return selectedFolder !== null || folderIsReal;
        if (selectedFolder !== null)
            return true; // Open on a folder steps into it
        return pickedFiles.length > 0 && (multiple || pickedFiles.length === 1);
    }

    readonly property string acceptText: {
        const given = (request.acceptLabel || "").replace(/_(?!_)/g, "").replace(/__/g, "_");
        if (given !== "")
            return given;
        if (saving || savingMany)
            return qsTr("Save");
        if (choosingFolder)
            return qsTr("Select “%1”").arg(Platform.baseName(chosenFolder) || chosenFolder);
        return qsTr("Open");
    }

    readonly property string statusText: {
        if (saving)
            return folderIsReal ? qsTr("Saving into %1").arg(Platform.baseName(folder) || folder)
                                : qsTr("Choose a folder to save into");
        if (savingMany)
            return qsTr("%n file(s) go into the folder you choose", "", (request.files || []).length);
        if (choosingFolder)
            return qsTr("Only folders are shown");
        if (selectedRows.length === 0)
            return Platform.formatItemCount(pane.files ? pane.files.count : 0);
        return qsTr("%n selected", "", selectedRows.length);
    }

    title: caption !== "" ? heading + " — " + appName : heading
    width: 1000
    height: 660
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    // Each region paints its own surface, as in the browser window, so the
    // backgroundOpacity alpha never stacks.
    color: "transparent"

    palette {
        window: Colors.chrome
        base: Colors.window
        alternateBase: Colors.chrome
        button: Colors.chrome
        active.text: Colors.text
        active.windowText: Colors.text
        active.buttonText: Colors.text
        inactive.text: Colors.text
        inactive.windowText: Colors.text
        inactive.buttonText: Colors.text
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

    // ---- answering ---------------------------------------------------------

    function choiceValues() {
        return choices.map(c => ({ id: c.id, value: choiceState[c.id] !== undefined
                                                    ? choiceState[c.id] : c.value }));
    }

    function finish(accepted, paths) {
        if (answered)
            return;
        answered = true;
        root.finished(accepted, accepted ? paths : [], filterIndex, choiceValues());
        root.close();
    }

    // The portal's Request.Close: the app no longer wants an answer.
    function abandon() {
        finish(false, []);
    }

    onClosing: {
        if (!answered) {
            answered = true;
            root.finished(false, [], filterIndex, []);
        }
    }

    function joinPath(dir, name) {
        return dir.endsWith("/") ? dir + name : dir + "/" + name;
    }

    function accept() {
        if (!acceptEnabled)
            return;
        if (saving) {
            saveAs(nameField.text);
        } else if (savingMany) {
            const names = request.files || [];
            const targets = names.map(n => joinPath(chosenFolder, Platform.baseName(n)));
            const existing = targets.filter(t => Platform.exists(t));
            if (existing.length > 0)
                overwriteConfirm.ask(targets, existing);
            else
                finish(true, targets);
        } else if (choosingFolder) {
            finish(true, [chosenFolder]);
        } else if (selectedFolder !== null && pickedFiles.length === 0) {
            pane.navigate(selectedFolder.path);
        } else {
            finish(true, pickedFiles.map(r => r.path));
        }
    }

    // Save under `typed`: a bare name lands in the open folder; a path (or a
    // folder's name) is followed first, as GTK's chooser does.
    function saveAs(typed) {
        const text = typed.trim();
        if (text === "" || !folderIsReal)
            return;
        let target = text.indexOf("/") >= 0 || text.startsWith("~")
                     ? Platform.resolvePath(text, folder) : joinPath(folder, text);
        if (Platform.isDir(target)) {
            pane.navigate(target);
            nameField.text = "";
            nameField.forceActiveFocus();
            return;
        }
        const parent = Platform.parentPath(target);
        if (!Platform.isDir(parent))
            return;
        if (Platform.exists(target))
            overwriteConfirm.ask([target], [target]);
        else
            finish(true, [target]);
    }

    // Enter or double-click on a file in the list.
    function chooseFile(path) {
        if (saving) {
            nameField.text = Platform.baseName(path);
            saveAs(nameField.text);
            return;
        }
        if (!openingFiles)
            return;
        const row = selectedRows.find(r => r.path === path);
        if (row && !row.matches)
            return;
        if (multiple && pickedFiles.length > 1 && pickedFiles.some(r => r.path === path))
            finish(true, pickedFiles.map(r => r.path));
        else
            finish(true, [path]);
    }

    // A path typed into the path bar: folders are walked to, a file is the
    // answer (open) or the name to save under (save).
    function goTo(target) {
        if (target === "")
            return;
        if (Platform.isDir(target) || target.indexOf("://") > 0) {
            pane.navigate(target);
        } else if (Platform.exists(target) && openingFiles) {
            finish(true, [target]);
        } else if (saving && Platform.isDir(Platform.parentPath(target))) {
            pane.navigate(Platform.parentPath(target));
            nameField.text = Platform.baseName(target);
        }
        returnFocus();
    }

    function returnFocus() {
        if (saving)
            nameField.forceActiveFocus();
        else
            pane.forceActiveFocus();
    }

    function openSearch() {
        searchOpen = true;
        searchField.text = pane.searchQuery;
        searchField.forceActiveFocus();
    }

    function closeSearch() {
        searchOpen = false;
        pane.searchQuery = "";
        returnFocus();
    }

    // In save mode a click on a file offers its name, so saving over it (or
    // next to it, with a tweak) is one edit away.
    onSelectedRowsChanged: {
        if (saving && selectedRows.length === 1 && !selectedRows[0].isDir)
            nameField.text = selectedRows[0].name;
    }

    Component.onCompleted: {
        const state = {};
        for (const c of choices)
            state[c.id] = c.value;
        choiceState = state;
        if (saving) {
            nameField.text = request.currentName || "";
            nameField.forceActiveFocus();
            // The stem, not the extension: the usual edit is the name.
            const dot = nameField.text.lastIndexOf(".");
            nameField.select(0, dot > 0 ? dot : nameField.text.length);
        } else {
            pane.forceActiveFocus();
        }
    }

    PickerFilter {
        id: pickerFilter
        rules: root.filterIndex >= 0 && root.filterIndex < root.filters.length
               ? root.filters[root.filterIndex].rules : []
    }

    // ---- keys --------------------------------------------------------------

    Shortcut {
        sequence: "Escape"
        // A dialog over the picker takes its own Escape.
        enabled: !overwriteConfirm.visible && !newFolderPrompt.visible && !credentialDialog.visible
        onActivated: {
            if (pathBar.editing)
                pathBar.cancelEditing();
            else if (root.searchOpen)
                root.closeSearch();
            else
                root.finish(false, []);
        }
    }
    // The accept button from the keyboard — how a folder is chosen without
    // the mouse, since Enter on a folder walks into it.
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; onActivated: root.accept() }
    Shortcut { sequence: "Ctrl+L"; onActivated: pathBar.beginEditing() }
    Shortcut { sequence: "Ctrl+A"; enabled: root.multiple; onActivated: pane.selectAll() }
    Shortcut { sequence: "Ctrl+F"; onActivated: root.openSearch() }
    Shortcut { sequence: "Ctrl+H"; onActivated: pane.showHidden = !pane.showHidden }
    Shortcut { sequence: "Alt+Left"; onActivated: pane.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: pane.goForward() }
    Shortcut { sequence: "Alt+Up"; onActivated: pane.goUp() }
    Shortcut { sequence: "Alt+Home"; onActivated: pane.navigate(Platform.homePath()) }
    Shortcut {
        sequence: "Ctrl+Shift+N"
        enabled: !root.openingFiles && root.folderIsReal
        onActivated: newFolderPrompt.ask()
    }
    // Typing a path straight into the list, as in GTK's chooser.
    Shortcut { sequence: "/"; onActivated: pathBar.beginTyping("/") }
    Shortcut { sequence: "~"; onActivated: pathBar.beginTyping("~/") }

    // ---- layout ------------------------------------------------------------

    component PickerButton: Rectangle {
        id: button

        property string label: ""
        property bool primary: false

        signal clicked()

        implicitWidth: Math.max(88, buttonLabel.implicitWidth + 32)
        implicitHeight: 34
        radius: Colors.radius
        opacity: enabled ? 1 : 0.45
        color: primary ? Colors.accent
             : buttonMouse.containsMouse && enabled ? Colors.hover : "transparent"
        border.color: primary ? Colors.accent : Colors.border
        border.width: 1

        Text {
            id: buttonLabel
            textFormat: Text.PlainText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, button.width - 16)
            elide: Text.ElideMiddle
            text: button.label
            color: button.primary ? Colors.window : Colors.text
            font.pixelSize: 13
            font.bold: button.primary
        }

        MouseArea {
            id: buttonMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: button.clicked()
        }
    }

    // A compact drop-down for file types and the app's own choices.
    component Dropdown: Rectangle {
        id: dropdown

        property var options: []   // [{ label }]
        property int currentIndex: 0

        signal picked(int index)

        implicitWidth: Math.min(300, dropdownLabel.implicitWidth + 40)
        implicitHeight: 32
        radius: Colors.radius
        color: dropdownMouse.containsMouse ? Colors.hover : Colors.window
        border.color: Colors.border
        border.width: 1

        Text {
            id: dropdownLabel
            textFormat: Text.PlainText
            anchors.left: parent.left
            anchors.right: arrow.left
            anchors.leftMargin: 10
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: dropdown.currentIndex >= 0 && dropdown.currentIndex < dropdown.options.length
                  ? dropdown.options[dropdown.currentIndex].label : ""
            color: Colors.text
            font.pixelSize: 13
        }

        Text {
            id: arrow
            textFormat: Text.PlainText
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Colors.textDim
            font.pixelSize: 12
        }

        MouseArea {
            id: dropdownMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: dropdownMenu.popup(dropdown, 0, dropdown.height)
        }

        Menu {
            id: dropdownMenu

            Instantiator {
                model: dropdown.options
                delegate: MenuItem {
                    required property int index
                    required property var modelData

                    contentItem: Text {
                        textFormat: Text.PlainText
                        leftPadding: 4
                        text: modelData.label
                        color: Colors.text
                        font.pixelSize: 13
                        font.bold: index === dropdown.currentIndex
                        verticalAlignment: Text.AlignVCenter
                    }
                    onTriggered: dropdown.picked(index)
                }
                onObjectAdded: (index, object) => dropdownMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => dropdownMenu.removeItem(object)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: what is being asked and by whom, then the browser's own
        // navigation row.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Colors.chrome

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 4

                Column {
                    Layout.preferredWidth: sidebar.width - 20
                    Layout.rightMargin: 8
                    spacing: 1

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        elide: Text.ElideRight
                        text: root.heading
                        color: Colors.text
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        visible: root.caption !== ""
                        elide: Text.ElideRight
                        text: root.caption
                        color: Colors.textDim
                        font.pixelSize: 11
                    }
                }

                ToolbarButton {
                    symbol: "←"
                    tip: qsTr("Back (Alt+Left)")
                    enabled: pane.history.canGoBack
                    onTriggered: pane.goBack()
                }

                ToolbarButton {
                    symbol: "→"
                    tip: qsTr("Forward (Alt+Right)")
                    enabled: pane.history.canGoForward
                    onTriggered: pane.goForward()
                }

                PathBar {
                    id: pathBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: !root.searchOpen
                    path: pane.path
                    showMenu: false
                    onNavigateRequested: target => root.goTo(target)
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: root.searchOpen
                    radius: Colors.radius
                    color: Colors.window
                    border.color: searchField.activeFocus ? Colors.accent : Colors.border
                    border.width: 1

                    TextField {
                        id: searchField
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        color: Colors.text
                        font.pixelSize: 13
                        background: null
                        placeholderText: qsTr("Search current folder")
                        selectByMouse: true
                        onTextEdited: pane.searchQuery = text
                        Keys.onDownPressed: {
                            pane.forceActiveFocus();
                            if (pane.files.count > 0)
                                pane.setCurrent(0, false);
                        }
                    }
                }

                ToolbarButton {
                    symbol: "⌕"
                    tip: qsTr("Search (Ctrl+F)")
                    active: root.searchOpen
                    onTriggered: root.searchOpen ? root.closeSearch() : root.openSearch()
                }

                ToolbarButton {
                    objectName: "pickerNewFolder"
                    visible: !root.openingFiles
                    symbol: "+"
                    tip: qsTr("New Folder (Ctrl+Shift+N)")
                    enabled: root.folderIsReal
                    onTriggered: newFolderPrompt.ask()
                }

                ToolbarButton {
                    visible: root.openingFiles
                    glyph: "view-sidebar"
                    tip: qsTr("Preview")
                    active: root.showPreview
                    onTriggered: root.showPreview = !root.showPreview
                }

                ToolbarButton {
                    symbol: pane.viewMode === "list" ? "▦" : "☰"
                    tip: pane.viewMode === "list" ? qsTr("Icon view") : qsTr("List view")
                    onTriggered: pane.viewMode = pane.viewMode === "list" ? "icon" : "list"
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.border
            }
        }

        // Save: the name comes first, focused, stem selected.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            visible: root.saving
            color: Colors.window

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Text {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: sidebar.width - 28
                    text: qsTr("Name")
                    color: Colors.textDim
                    font.pixelSize: 13
                }

                TextField {
                    id: nameField
                    objectName: "pickerName"
                    Layout.preferredWidth: 480
                    Layout.preferredHeight: 34
                    color: Colors.text
                    font.pixelSize: 13
                    selectByMouse: true
                    background: Rectangle {
                        radius: Colors.radius
                        color: Colors.chrome
                        border.color: nameField.activeFocus ? Colors.accent : Colors.border
                        border.width: nameField.activeFocus ? 2 : 1
                    }
                    onAccepted: root.accept()
                    Keys.onDownPressed: pane.forceActiveFocus()
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.border
            }
        }

        // Places, files, preview.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Sidebar {
                id: sidebar
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 200
                picker: true
                currentLocation: pane.path
                mounter: pickerMounter
                onNavigateRequested: location => {
                    pane.navigate(location);
                    root.returnFocus();
                }
            }

            Rectangle {
                anchors.left: sidebar.right
                anchors.right: preview.visible ? preview.left : parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: Colors.window

                Tab {
                    id: pane
                    objectName: "pickerPane"
                    anchors.fill: parent
                    path: root.request.folder || Platform.homePath()
                    pickerMode: true
                    // A list reads names and dates at a glance, which is
                    // what choosing among files is mostly about.
                    viewMode: "list"
                    pickerFilter: pickerFilter
                    foldersOnly: root.choosingFolder
                    onFileChosen: path => root.chooseFile(path)
                    onMountNeeded: location => pickerMounter.mountLocation(location)
                }
            }

            // What is selected, before it is handed over.
            Rectangle {
                id: preview

                readonly property var row: root.selectedRows.length === 1 && !root.selectedRows[0].isDir
                                           ? root.selectedRows[0] : null
                readonly property bool thumbnailed: row !== null
                    && Settings.showThumbnails !== "never"
                    && (Settings.showThumbnails === "always" || Platform.isLocal(row.path))
                    && Thumbnails.canThumbnail(row.contentType, row.size)

                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 250
                visible: root.openingFiles && root.showPreview && root.width >= 820
                color: Colors.chrome

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Colors.border
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Rectangle {
                        width: parent.width
                        height: 170
                        radius: Colors.radius
                        color: Colors.window

                        Image {
                            anchors.fill: parent
                            anchors.margins: preview.thumbnailed ? 0 : 40
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize: Qt.size(256, 256)
                            source: preview.row === null
                                    ? Colors.fileIcon(root.selectedRows.length > 1
                                                      ? "image://fileicon/text-x-generic"
                                                      : "image://fileicon/folder",
                                                      Colors.accent, 64)
                                    : preview.thumbnailed
                                    ? Thumbnails.source(preview.row.path, preview.row.modified, preview.row.size)
                                    : Colors.fileIcon(preview.row.iconSource, Colors.textDim, 64)
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        text: preview.row !== null ? preview.row.name
                            : root.selectedRows.length > 1 ? qsTr("%n items", "", root.selectedRows.length)
                            : (Platform.baseName(root.folder) || root.folder)
                        color: Colors.text
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Grid {
                        visible: preview.row !== null
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 6

                        Text { textFormat: Text.PlainText; text: qsTr("Size"); color: Colors.textDim; font.pixelSize: 12 }
                        Text {
                            textFormat: Text.PlainText
                            text: preview.row ? Platform.formatSize(preview.row.size) : ""
                            color: Colors.text
                            font.pixelSize: 12
                        }
                        Text { textFormat: Text.PlainText; text: qsTr("Type"); color: Colors.textDim; font.pixelSize: 12 }
                        Text {
                            textFormat: Text.PlainText
                            width: 150
                            elide: Text.ElideRight
                            text: preview.row ? preview.row.typeDescription : ""
                            color: Colors.text
                            font.pixelSize: 12
                        }
                        Text { textFormat: Text.PlainText; text: qsTr("Modified"); color: Colors.textDim; font.pixelSize: 12 }
                        Text {
                            textFormat: Text.PlainText
                            text: preview.row ? Platform.formatModified(preview.row.modified) : ""
                            color: Colors.text
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        visible: preview.row !== null && !preview.row.matches
                        wrapMode: Text.Wrap
                        text: qsTr("Not a file type this app asked for")
                        color: Colors.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        // Footer: file type, the app's choices, and the answer.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Colors.chrome

            Rectangle {
                width: parent.width
                height: 1
                color: Colors.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    textFormat: Text.PlainText
                    visible: root.filters.length > 0
                    text: root.saving ? qsTr("Type") : qsTr("Show")
                    color: Colors.textDim
                    font.pixelSize: 13
                }

                Dropdown {
                    objectName: "pickerFilter"
                    visible: root.filters.length > 0
                    options: root.filters.map(f => ({ label: f.name }))
                    currentIndex: root.filterIndex
                    onPicked: index => root.filterIndex = index
                }

                Repeater {
                    model: root.choices

                    delegate: Item {
                        id: choice

                        required property var modelData
                        readonly property bool isCheck: (modelData.options || []).length === 0
                        readonly property string value: root.choiceState[modelData.id] !== undefined
                                                        ? root.choiceState[modelData.id] : modelData.value

                        function set(value) {
                            const next = Object.assign({}, root.choiceState);
                            next[modelData.id] = value;
                            root.choiceState = next;
                        }

                        implicitWidth: row.implicitWidth
                        implicitHeight: 32

                        Row {
                            id: row
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            // A checkbox when the app gave no options.
                            Rectangle {
                                visible: choice.isCheck
                                anchors.verticalCenter: parent.verticalCenter
                                width: 18
                                height: 18
                                radius: 4
                                color: choice.value === "true" ? Colors.accent : "transparent"
                                border.color: choice.value === "true" ? Colors.accent : Colors.border
                                border.width: 1

                                Text {
                                    textFormat: Text.PlainText
                                    anchors.centerIn: parent
                                    visible: choice.value === "true"
                                    text: "✓"
                                    color: Colors.window
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: choice.set(choice.value === "true" ? "false" : "true")
                                }
                            }

                            Text {
                                textFormat: Text.PlainText
                                anchors.verticalCenter: parent.verticalCenter
                                text: choice.modelData.label
                                color: choice.isCheck ? Colors.text : Colors.textDim
                                font.pixelSize: 13

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: choice.isCheck
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: choice.set(choice.value === "true" ? "false" : "true")
                                }
                            }

                            Dropdown {
                                visible: !choice.isCheck
                                anchors.verticalCenter: parent.verticalCenter
                                options: choice.modelData.options || []
                                currentIndex: (choice.modelData.options || []).findIndex(o => o.id === choice.value)
                                onPicked: index => choice.set(choice.modelData.options[index].id)
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    textFormat: Text.PlainText
                    Layout.maximumWidth: 260
                    elide: Text.ElideMiddle
                    text: root.statusText
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                PickerButton {
                    objectName: "pickerCancel"
                    label: qsTr("Cancel")
                    onClicked: root.finish(false, [])
                }

                PickerButton {
                    objectName: "pickerAccept"
                    Layout.maximumWidth: 280
                    primary: true
                    label: root.acceptText
                    enabled: root.acceptEnabled
                    onClicked: root.accept()
                }
            }
        }
    }

    // ---- dialogs -----------------------------------------------------------

    PromptDialog {
        id: newFolderPrompt
        prompt: qsTr("Name for the new folder")
        initialText: qsTr("New Folder")
        onClosed: root.returnFocus()
        onAccepted_: name => FileOperations.createFolder(root.folder, name)
    }

    ConfirmDialog {
        id: overwriteConfirm

        property var targets: []

        function ask(allTargets, existing) {
            targets = allTargets;
            message = existing.length === 1
                ? qsTr("Replace “%1”?").arg(Platform.baseName(existing[0]))
                : qsTr("Replace %n files?", "", existing.length);
            detail = existing.length === 1
                ? qsTr("A file with this name is already in “%1”. Saving replaces it.")
                      .arg(Platform.baseName(Platform.parentPath(existing[0])))
                : existing.map(p => Platform.baseName(p)).join(", ");
            open();
        }

        confirmText: qsTr("Replace")
        onConfirmed: root.finish(true, targets)
        onClosed: root.returnFocus()
    }

    Mounter {
        id: pickerMounter
        onMounted: location => {
            if (pane.path === location)
                pane.reload();
        }
        onMountFailed: (location, message) => {
            if (message === "" && pane.path === location)
                pane.goBack();
        }
        onPromptAborted: credentialDialog.abort()
        // No server-verification dialog here: a mount that needs one is
        // declined, and the browser window is the place to trust a server.
        onAskQuestion: (message, choices) => pickerMounter.cancelPassword()
        onAskPassword: (message, defaultUser, defaultDomain, needsUsername, needsDomain, needsPassword, canAnonymous) => {
            credentialDialog.ask(message, defaultUser, defaultDomain,
                                 needsUsername, needsDomain, needsPassword, canAnonymous);
        }
    }

    CredentialDialog {
        id: credentialDialog
        onClosed: root.returnFocus()
        onAnswered: (username, domain, password, anonymous, remember) =>
            pickerMounter.providePassword(username, domain, password, anonymous, remember)
        onDismissed: pickerMounter.cancelPassword()
    }
}
