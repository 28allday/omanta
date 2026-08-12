import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Detail view. Columns come from Settings (Visible Columns… in the
// view-options menu); every header sorts. Name is always present and takes
// the flexible width.
Item {
    id: root

    required property var tab

    property alias currentIndex: view.currentIndex

    // Label, sort key and width per column id. Width 0 marks the flex column.
    readonly property var columnMeta: ({
        name:        { label: qsTr("Name"),        sortKey: FileSortFilterModel.ByName,        width: 0 },
        size:        { label: qsTr("Size"),        sortKey: FileSortFilterModel.BySize,        width: 120 },
        type:        { label: qsTr("Type"),        sortKey: FileSortFilterModel.ByType,        width: 170 },
        owner:       { label: qsTr("Owner"),       sortKey: FileSortFilterModel.ByOwner,       width: 110 },
        group:       { label: qsTr("Group"),       sortKey: FileSortFilterModel.ByGroup,       width: 110 },
        permissions: { label: qsTr("Permissions"), sortKey: FileSortFilterModel.ByPermissions, width: 110 },
        modified:    { label: qsTr("Modified"),    sortKey: FileSortFilterModel.ByModified,    width: 150 },
        created:     { label: qsTr("Created"),     sortKey: FileSortFilterModel.ByCreated,     width: 150 },
        accessed:    { label: qsTr("Accessed"),    sortKey: FileSortFilterModel.ByAccessed,    width: 150 }
    })

    // The live column set — follows the Settings notify, so a change in the
    // Visible Columns dialog (or a hand edit of the settings file) lands here
    // without a reload.
    readonly property var listColumns: Settings.listVisibleColumns

    readonly property real fixedWidth: {
        let total = 0;
        for (const id of listColumns)
            if (id !== "name")
                total += columnMeta[id].width;
        return total;
    }

    function cellText(id, row) {
        switch (id) {
        case "size": return row.isDir
                          ? Platform.formatItemCount(root.tab.showHidden ? row.itemCountAll
                                                                         : row.itemCount)
                          : Platform.formatSize(row.size);
        case "type": return row.typeDescription;
        case "owner": return row.owner || "—";
        case "group": return row.group || "—";
        case "permissions": return row.permissions || "—";
        case "modified": return Platform.formatModified(row.modified, Settings.dateTimeFormat);
        case "created": return isNaN(row.created) ? "—"
                             : Platform.formatModified(row.created, Settings.dateTimeFormat);
        case "accessed": return isNaN(row.accessed) ? "—"
                             : Platform.formatModified(row.accessed, Settings.dateTimeFormat);
        }
        return "";
    }

    function positionAt(row) { view.positionViewAtIndex(row, ListView.Contain); }

    // The current row's rendered Size cell, through the same cellText the
    // delegates use — how verify-ui asserts the item-count chain end-to-end.
    readonly property string currentSizeCell: view.currentItem
                                              ? cellText("size", view.currentItem) : ""

    // The current row's tree state, read off the live delegate the same way.
    readonly property int currentDepth: view.currentItem ? view.currentItem.depth : 0
    readonly property bool currentExpanded: view.currentItem
                                            ? view.currentItem.expanded : false

    Column {
        anchors.fill: parent

        // Header
        Rectangle {
            width: parent.width
            height: 28
            color: Colors.chrome

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10

                Repeater {
                    model: root.listColumns

                    delegate: Item {
                        required property string modelData

                        readonly property var meta: root.columnMeta[modelData]

                        height: parent.height
                        width: meta.width > 0 ? meta.width
                                              : root.width - 20 - root.fixedWidth

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Text {
                                text: meta.label
                                color: root.tab.sortKey === meta.sortKey ? Colors.text : Colors.textDim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            Text {
                                visible: root.tab.sortKey === meta.sortKey
                                text: root.tab.sortDescending ? "▾" : "▴"
                                color: Colors.accent
                                font.pixelSize: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.tab.setSort(meta.sortKey)
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.border
            }
        }

        Item {
            width: parent.width
            height: parent.height - 28

            // Empty space accepts drops into the folder being viewed. Behind
            // the list, so folder rows' own DropAreas win where they overlap.
            DropArea {
                anchors.fill: parent
                onDropped: drop => {
                    root.tab.requestDrop(drop.urls, root.tab.path);
                    drop.accept();
                }
            }

            ListView {
            id: view

            anchors.fill: parent
            model: root.tab.files
            clip: true
            focus: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            // Navigation is driven by Tab so list and grid behave identically.
            keyNavigationEnabled: false

            ScrollBar.vertical: ScrollBar { id: vbar }

            delegate: Rectangle {
                id: row

                required property int index
                required property string name
                required property string displayName
                required property bool isDir
                required property real size
                required property string typeDescription
                required property var modified
                required property var created
                required property var accessed
                required property string owner
                required property string group
                required property string permissions
                required property string iconSource
                required property string filePath
                required property string targetPath
                required property string contentType
                required property int itemCount
                required property int itemCountAll
                required property int depth
                required property bool expanded

                // The expander's horizontal band, shared by the glyph and the
                // press/click guards in rowMouse — one definition, no drift.
                readonly property real expanderX0: 10 + depth * 18
                readonly property real expanderX1: expanderX0 + 18
                function inExpander(x) {
                    return root.tab.treeActive && isDir
                        && x >= expanderX0 && x <= expanderX1;
                }

                width: view.width
                height: Colors.rowHeight
                color: root.tab.isSelected(name) ? Colors.selection
                     : rowMouse.containsMouse ? Colors.hover
                     : "transparent"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 0

                    Item {
                        width: row.width - 20 - root.fixedWidth
                        height: parent.height

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            // Tree furniture: indentation plus the expander
                            // slot. One item so the click zone is one band;
                            // files get the empty band too, keeping columns
                            // aligned within a level.
                            Item {
                                visible: root.tab.treeActive
                                width: row.depth * 18 + 14
                                height: Colors.rowHeight

                                Text {
                                    visible: row.isDir
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: row.expanded ? "▾" : "▸"
                                    color: row.expanded ? Colors.accent : Colors.textDim
                                    font.pixelSize: 10
                                }
                            }

                            Image {
                                id: rowPreview

                                property bool thumbnailFailed: false
                                // recent:/// rows point at a real file elsewhere.
                                readonly property string previewPath:
                                    row.targetPath !== "" ? row.targetPath : row.filePath
                                readonly property bool wantThumbnail:
                                    !thumbnailFailed
                                    && Settings.showThumbnails !== "never"
                                    && (Settings.showThumbnails === "always"
                                        || Platform.isLocal(previewPath))
                                    && Thumbnails.canThumbnail(row.contentType, row.size)

                                width: 18
                                height: 18
                                anchors.verticalCenter: parent.verticalCenter
                                fillMode: Image.PreserveAspectFit
                                source: wantThumbnail ? "image://thumbnail/" + rowPreview.previewPath
                                                      : Colors.tint(row.iconSource,
                                                            root.tab.isSelected(row.name) ? Colors.selectionText
                                                          : row.isDir ? Colors.accent
                                                          : Colors.textDim)
                                sourceSize: Qt.size(18, 18)
                                asynchronous: true
                                cache: true
                                onStatusChanged: if (status === Image.Error && wantThumbnail)
                                                     thumbnailFailed = true
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.displayName
                                color: root.tab.isSelected(row.name) ? Colors.selectionText : Colors.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth,
                                                row.width - root.fixedWidth - 60
                                                - (root.tab.treeActive ? row.depth * 18 + 22 : 0))
                            }
                        }
                    }

                    Repeater {
                        model: root.listColumns.slice(1) // name renders above

                        Text {
                            required property string modelData

                            width: root.columnMeta[modelData].width
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: root.cellText(modelData, row)
                            color: Colors.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }

                // Folders take drops directly; the hairline highlight is the
                // selection colour so the target is unmistakable.
                DropArea {
                    id: rowDrop

                    anchors.fill: parent
                    enabled: row.isDir
                    onDropped: drop => {
                        root.tab.requestDrop(drop.urls, row.filePath);
                        drop.accept();
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: rowDrop.containsDrag
                    color: "transparent"
                    border.color: Colors.accent
                    border.width: 1
                    radius: Colors.radius
                }

                // Invisible peg the native drag hangs off. Automatic drags
                // hand the platform a text/uri-list, so files can be dragged
                // into other applications, not just between omanta views.
                Item {
                    id: dragProxy

                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                    Drag.active: rowMouse.drag.active
                    Drag.hotSpot: Qt.point(9, 9)
                }

                MouseArea {
                    id: rowMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    drag.target: dragProxy

                    onPressed: mouse => {
                        if (mouse.button !== Qt.LeftButton)
                            return;
                        // A press on the expander is a toggle in the making,
                        // not a selection or a drag — as in Nautilus.
                        if (row.inExpander(mouse.x))
                            return;
                        // Pressing an unselected row selects it before any
                        // drag can begin — dragging carries what you grabbed,
                        // Nautilus-style. Modifier presses keep their meaning
                        // for the click on release.
                        if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                            && !root.tab.isSelected(row.name))
                            root.tab.selectOnly(row.name);
                        const paths = root.tab.isSelected(row.name)
                                    ? root.tab.selectedPaths()
                                    : [root.tab.viewingRecent && row.targetPath !== ""
                                       ? row.targetPath : row.filePath];
                        dragProxy.Drag.mimeData = { "text/uri-list": Platform.uriList(paths) };
                        row.grabToImage(result => dragProxy.Drag.imageSource = result.url);
                    }

                    onClicked: mouse => {
                        if (mouse.button === Qt.LeftButton && row.inExpander(mouse.x)) {
                            root.tab.toggleExpand(row.index);
                            return;
                        }
                        root.tab.currentIndex = row.index;
                        if (mouse.button === Qt.RightButton) {
                            if (!root.tab.isSelected(row.name))
                                root.tab.selectOnly(row.name);
                            root.tab.requestContextMenu();
                            return;
                        }
                        if (mouse.modifiers & Qt.ControlModifier)
                            root.tab.toggleSelection(row.name);
                        else if (mouse.modifiers & Qt.ShiftModifier)
                            root.tab.extendSelectionTo(row.index);
                        else {
                            root.tab.selectOnly(row.name);
                            // Single-click policy: a plain click opens.
                            // Modifier clicks stay selection, as Nautilus.
                            if (Settings.clickPolicy === "single")
                                root.tab.activate(row.index);
                        }
                    }

                    onDoubleClicked: mouse => {
                        // The second rapid click on the expander is another
                        // toggle, never an open.
                        if (mouse.button === Qt.LeftButton && row.inExpander(mouse.x)) {
                            root.tab.toggleExpand(row.index);
                            return;
                        }
                        if (mouse.button === Qt.LeftButton
                            && Settings.clickPolicy !== "single")
                            root.tab.activate(row.index);
                    }
                }
            }
            }

            // Rubber-band selection. An overlay ABOVE the view: a press on a
            // row is rejected so it falls through to the row (click, DnD); a
            // press on empty space below the rows starts the band. It cannot
            // live inside the view at z:-1 — an interactive Flickable
            // accepts every press itself before its negative-z children see
            // anything (probed 2026-08-08; the band had never worked).
            MouseArea {
                id: rubber

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // The Flickable must not steal the grab mid-band.
                preventStealing: true

                property real originX: 0
                property real originY: 0 // content coords, survives scrolling
                property real curX: 0
                property real curY: 0
                property bool active: false

                onPressed: mouse => {
                    // The overlay sits above the scrollbar too — its strip
                    // belongs to it.
                    if (vbar.visible && mouse.x >= view.width - vbar.width) {
                        mouse.accepted = false;
                        return;
                    }
                    if (view.indexAt(mouse.x + view.contentX,
                                     mouse.y + view.contentY) !== -1) {
                        mouse.accepted = false; // over a row — the row's press
                        return;
                    }
                    // Background right-click: the folder's own menu (Paste,
                    // New Folder…), acting on no selection — as Nautilus.
                    if (mouse.button === Qt.RightButton) {
                        root.tab.clearSelection();
                        root.tab.requestContextMenu();
                        return;
                    }
                    root.tab.clearSelection();
                    originX = mouse.x;
                    originY = mouse.y + view.contentY;
                    curX = mouse.x;
                    curY = mouse.y;
                    active = true;
                }
                onPositionChanged: mouse => {
                    if (!active)
                        return;
                    curX = mouse.x;
                    curY = mouse.y;
                    const y1 = Math.min(originY, mouse.y + view.contentY);
                    const y2 = Math.max(originY, mouse.y + view.contentY);
                    // Row geometry is uniform, so the hit range is arithmetic
                    // rather than a walk over delegates that may not exist yet.
                    const first = Math.max(0, Math.floor(y1 / Colors.rowHeight));
                    const last = Math.min(root.tab.files.count - 1,
                                          Math.floor(y2 / Colors.rowHeight));
                    if (last >= first)
                        root.tab.selectRange(first, last);
                    else
                        root.tab.clearSelection(); // band shrank off the rows
                }
                onReleased: active = false
                onCanceled: active = false
            }

            Rectangle {
                visible: rubber.active
                x: Math.min(rubber.originX, rubber.curX)
                y: Math.min(rubber.originY - view.contentY, rubber.curY)
                width: Math.abs(rubber.curX - rubber.originX)
                height: Math.abs(rubber.curY - (rubber.originY - view.contentY))
                color: Qt.alpha(Colors.accent, 0.12)
                border.color: Colors.accent
                border.width: 1
            }
        }
    }
}
