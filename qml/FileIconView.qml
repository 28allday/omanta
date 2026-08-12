import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Grid view. Zoom changes the cell size; the delegate scales with it.
Item {
    id: root

    required property var tab

    property alias currentIndex: view.currentIndex
    readonly property int iconSize: root.tab.zoom
    readonly property int cellW: iconSize + 46
    readonly property int cellH: iconSize + 52 + captions.length * 14

    // Nautilus's graded caption reveal — "More information will appear when
    // zooming closer": none at 32–48, one at 64–80, two at 96–112, all
    // three at 128.
    readonly property int captionSlots: iconSize >= 128 ? 3
                                      : iconSize >= 96 ? 2
                                      : iconSize >= 64 ? 1 : 0
    readonly property var captions: {
        const out = [];
        const chosen = Settings.iconCaptions;
        for (let i = 0; i < Math.min(captionSlots, chosen.length); ++i)
            if (chosen[i] !== "none")
                out.push(chosen[i]);
        return out;
    }

    function captionText(id, cell) {
        switch (id) {
        case "size": return cell.isDir
                          ? Platform.formatItemCount(root.tab.showHidden ? cell.itemCountAll
                                                                         : cell.itemCount)
                          : Platform.formatSize(cell.size);
        case "type": return cell.typeDescription;
        case "owner": return cell.owner || "—";
        case "group": return cell.group || "—";
        case "permissions": return cell.permissions || "—";
        case "modified": return Platform.formatModified(cell.modified, Settings.dateTimeFormat);
        case "created": return isNaN(cell.created) ? "—"
                             : Platform.formatModified(cell.created, Settings.dateTimeFormat);
        case "accessed": return isNaN(cell.accessed) ? "—"
                             : Platform.formatModified(cell.accessed, Settings.dateTimeFormat);
        }
        return "";
    }

    // How many cells fit across — the tab needs this to make Up/Down move a
    // whole row rather than a single item.
    readonly property int columns: Math.max(1, Math.floor(view.width / view.cellWidth))

    function positionAt(row) { view.positionViewAtIndex(row, GridView.Contain); }

    // Empty space accepts drops into the folder being viewed. Behind the
    // grid, so folder cells' own DropAreas win where they overlap.
    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            root.tab.requestDrop(drop.urls, root.tab.path);
            drop.accept();
        }
    }

    GridView {
        id: view

        anchors.fill: parent
        anchors.margins: 8
        model: root.tab.files
        cellWidth: root.cellW
        cellHeight: root.cellH
        clip: true
        focus: true
        boundsBehavior: Flickable.StopAtBounds
        // Navigation is driven by Tab so list and grid behave identically.
        keyNavigationEnabled: false

        ScrollBar.vertical: ScrollBar { id: vbar }

        delegate: Item {
            id: cell

            required property int index
            required property string name
            required property string displayName
            required property string iconSource
            required property string filePath
            required property string targetPath
            required property string contentType
            required property real size
            required property bool isDir
            required property string typeDescription
            required property var modified
            required property var created
            required property var accessed
            required property string owner
            required property string group
            required property string permissions
            required property int itemCount
            required property int itemCountAll

            width: view.cellWidth
            height: view.cellHeight

            // The rubber overlay asks whether a press landed on the visible
            // content (icon + label) or in the cell's padding — padding
            // starts a band, as in Nautilus.
            function contentHit(lx, ly) {
                return lx >= body.x && lx <= body.x + body.width
                    && ly >= body.y && ly <= body.y + body.height;
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: Colors.radius
                color: root.tab.isSelected(cell.name) ? Colors.selection
                     : cellMouse.containsMouse ? Colors.hover
                     : "transparent"
            }

            Column {
                id: body
                anchors.centerIn: parent
                spacing: 6
                width: parent.width - 12

                Image {
                    id: preview

                    // A thumbnail that fails is a normal outcome — an unreadable
                    // JPEG, a codec the decoder doesn't have — so the delegate
                    // quietly falls back to the file-type icon rather than
                    // showing a broken image.
                    property bool thumbnailFailed: false
                    // recent:/// rows point at a real file elsewhere; the
                    // thumbnail cache and generators want that, not the row.
                    readonly property string previewPath:
                        cell.targetPath !== "" ? cell.targetPath : cell.filePath
                    readonly property bool wantThumbnail:
                        !thumbnailFailed
                        && Settings.showThumbnails !== "never"
                        && (Settings.showThumbnails === "always"
                            || Platform.isLocal(previewPath))
                        && Thumbnails.canThumbnail(cell.contentType, cell.size)

                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.iconSize
                    height: root.iconSize
                    fillMode: Image.PreserveAspectFit
                    source: wantThumbnail ? "image://thumbnail/" + preview.previewPath
                                          : Colors.tint(cell.iconSource,
                                                root.tab.isSelected(cell.name) ? Colors.selectionText
                                              : cell.isDir ? Colors.accent
                                              : Colors.textDim)
                    sourceSize: Qt.size(root.iconSize, root.iconSize)
                    asynchronous: true
                    cache: true
                    onStatusChanged: if (status === Image.Error && wantThumbnail) thumbnailFailed = true
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.displayName
                    color: root.tab.isSelected(cell.name) ? Colors.selectionText : Colors.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: root.captions

                    Text {
                        required property string modelData

                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.captionText(modelData, cell)
                        color: root.tab.isSelected(cell.name) ? Colors.selectionText : Colors.textDim
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                    }
                }
            }

            DropArea {
                id: cellDrop

                anchors.fill: parent
                enabled: cell.isDir
                onDropped: drop => {
                    root.tab.requestDrop(drop.urls, cell.filePath);
                    drop.accept();
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                visible: cellDrop.containsDrag
                color: "transparent"
                border.color: Colors.accent
                border.width: 1
                radius: Colors.radius
            }

            // Invisible peg the native drag hangs off — see FileListView.
            Item {
                id: dragProxy

                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                Drag.active: cellMouse.drag.active
                Drag.hotSpot: Qt.point(9, 9)
            }

            MouseArea {
                id: cellMouse

                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: dragProxy

                onPressed: mouse => {
                    if (mouse.button !== Qt.LeftButton)
                        return;
                    if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                        && !root.tab.isSelected(cell.name))
                        root.tab.selectOnly(cell.name);
                    const paths = root.tab.isSelected(cell.name)
                                ? root.tab.selectedPaths()
                                : [root.tab.viewingRecent && cell.targetPath !== ""
                                   ? cell.targetPath : cell.filePath];
                    dragProxy.Drag.mimeData = { "text/uri-list": Platform.uriList(paths) };
                    cell.grabToImage(result => dragProxy.Drag.imageSource = result.url);
                }

                onClicked: mouse => {
                    root.tab.currentIndex = cell.index;
                    if (mouse.button === Qt.RightButton) {
                        if (!root.tab.isSelected(cell.name))
                            root.tab.selectOnly(cell.name);
                        root.tab.requestContextMenu();
                        return;
                    }
                    if (mouse.modifiers & Qt.ControlModifier)
                        root.tab.toggleSelection(cell.name);
                    else if (mouse.modifiers & Qt.ShiftModifier)
                        root.tab.extendSelectionTo(cell.index);
                    else {
                        root.tab.selectOnly(cell.name);
                        // Single-click policy: a plain click opens.
                        // Modifier clicks stay selection, as Nautilus.
                        if (Settings.clickPolicy === "single")
                            root.tab.activate(cell.index);
                    }
                }

                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton
                        && Settings.clickPolicy !== "single")
                        root.tab.activate(cell.index);
                }
            }
        }
    }

    // Rubber-band selection. An overlay ABOVE the view: a press on a cell's
    // icon or label falls through to the cell (click, DnD); a press on empty
    // space — or in a cell's padding, as in Nautilus — starts the band. It
    // cannot live inside the view at z:-1 — an interactive Flickable accepts
    // every press itself before its negative-z children see anything
    // (probed 2026-08-08; the band had never worked).
    MouseArea {
        id: rubber

        anchors.fill: view
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // The Flickable must not steal the grab mid-band.
        preventStealing: true

        property real originX: 0
        property real originY: 0 // content coords, survives scrolling
        property real curX: 0
        property real curY: 0
        property bool active: false

        onPressed: mouse => {
            // The overlay sits above the scrollbar too — its strip belongs
            // to it.
            if (vbar.visible && mouse.x >= view.width - vbar.width) {
                mouse.accepted = false;
                return;
            }
            const cx = mouse.x + view.contentX;
            const cy = mouse.y + view.contentY;
            const index = view.indexAt(cx, cy);
            if (index !== -1) {
                const item = view.itemAtIndex(index);
                if (item && item.contentHit(cx - item.x, cy - item.y)) {
                    mouse.accepted = false; // on the icon/label — the cell's
                    return;
                }
            }
            // Background right-click: the folder's own menu (Paste, New
            // Folder…), acting on no selection — as Nautilus.
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
            const x1 = Math.min(originX, mouse.x);
            const x2 = Math.max(originX, mouse.x);
            const y1 = Math.min(originY, mouse.y + view.contentY);
            const y2 = Math.max(originY, mouse.y + view.contentY);

            // Cells are a uniform grid, so which ones the band covers is
            // arithmetic — no need for delegates to exist to be selected.
            const columns = Math.max(1, Math.floor(view.width / view.cellWidth));
            const firstCol = Math.max(0, Math.floor(x1 / view.cellWidth));
            const lastCol = Math.min(columns - 1, Math.floor(x2 / view.cellWidth));
            const firstRow = Math.max(0, Math.floor(y1 / view.cellHeight));
            const lastRow = Math.floor(y2 / view.cellHeight);

            const names = [];
            for (let r = firstRow; r <= lastRow; ++r) {
                for (let c = firstCol; c <= lastCol; ++c) {
                    const index = r * columns + c;
                    if (index >= 0 && index < root.tab.files.count)
                        names.push(root.tab.files.valueAt(index, "name"));
                }
            }
            root.tab.selectNames(names);
        }
        onReleased: active = false
        onCanceled: active = false
    }

    Rectangle {
        visible: rubber.active
        x: rubber.x + Math.min(rubber.originX, rubber.curX)
        y: rubber.y + Math.min(rubber.originY - view.contentY, rubber.curY)
        width: Math.abs(rubber.curX - rubber.originX)
        height: Math.abs(rubber.curY - (rubber.originY - view.contentY))
        color: Qt.alpha(Colors.accent, 0.12)
        border.color: Colors.accent
        border.width: 1
    }
}
