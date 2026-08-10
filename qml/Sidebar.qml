import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// The places sidebar: user folders, devices, bookmarks, network. Every colour
// comes from Colors, so it wears the active Omarchy theme like the rest of
// the app — flat surfaces, subtle borders, the accent only where it means
// something.
Rectangle {
    id: root

    // The location the window is showing, for highlighting the matching row.
    property string currentLocation: ""

    // The window's Mounter — volumes that need credentials ask through it.
    property Mounter mounter: null

    signal navigateRequested(string location)
    signal mountError(string name, string message)
    // Files were dropped on a row; the window decides what that means
    // (Trash trashes, anywhere else transfers).
    signal dropRequested(var urls, string location)
    signal openInNewTabRequested(string location)
    // The window owns the confirm dialog; emptying is never one click.
    signal emptyTrashRequested()
    // The operations popover closed; the window hands the keyboard back —
    // the same contract every dialog honours (see the Phase 3 findings).
    signal opsPopoverClosed()

    function isBookmarked(location) {
        return places.isBookmarked(location);
    }

    // Bookmark the folder or drop the bookmark — Ctrl+D in the window.
    function toggleBookmark(location) {
        if (places.isBookmarked(location))
            places.removeBookmark(location);
        else
            places.addBookmark(location);
    }

    implicitWidth: 200
    color: Colors.chrome

    PlacesModel {
        id: places
        mounter: root.mounter
    }

    // Model count published for the UI verification script.
    readonly property int placesCount: places.count

    Connections {
        target: places
        function onMounted(location) {
            root.navigateRequested(location);
        }
        function onMountFailed(name, message) {
            root.mountError(name, message);
        }
    }

    ListView {
        id: list

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: opsArea.top
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        clip: true
        model: places
        boundsBehavior: Flickable.StopAtBounds

        // Nautilus draws no section headers — just a hairline between the
        // fixed places, the bookmarks and the devices. Same here.
        section.property: "section"
        section.delegate: Item {
            required property string section

            width: list.width
            height: section === "Places" ? 4 : 15

            Rectangle {
                visible: parent.section !== "Places"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: list.width - 28
                height: 1
                color: Colors.border
            }
        }

        delegate: Rectangle {
            id: row

            required property int index
            required property string name
            required property string location
            required property string iconSource
            required property string section
            required property bool mountable
            required property bool ejectable

            readonly property bool current: location !== "" && location === root.currentLocation
            // Recent is read-only and Network is not a folder; everywhere
            // else with a location can take a drop — dropping on Starred
            // stars, on Trash trashes, elsewhere transfers (Nautilus's rules).
            readonly property bool droppable: location !== ""
                                              && location !== "recent:///"
                                              && location !== "network:///"

            width: list.width - 12
            x: 6
            height: 30
            radius: Colors.radius
            color: current ? Colors.selection
                 : rowDrop.containsDrag ? Colors.hover
                 : rowMouse.containsMouse ? Colors.hover : "transparent"
            border.color: rowDrop.containsDrag ? Colors.accent : "transparent"
            border.width: rowDrop.containsDrag ? 1 : 0

            DropArea {
                id: rowDrop

                anchors.fill: parent
                enabled: row.droppable
                onDropped: drop => {
                    root.dropRequested(drop.urls, row.location);
                    drop.accept();
                }
            }

            Image {
                id: icon

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                source: Colors.tint(row.iconSource,
                                    row.current ? Colors.selectionText : Colors.textDim)
                sourceSize: Qt.size(16, 16)
                opacity: row.mountable ? 0.6 : 1
            }

            Text {
                anchors.left: icon.right
                anchors.leftMargin: 8
                anchors.right: ejectButton.visible ? ejectButton.left : parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                color: row.current ? Colors.selectionText : row.mountable ? Colors.textDim : Colors.text
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Text {
                id: ejectButton

                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: row.ejectable
                text: "⏏"
                color: ejectMouse.containsMouse ? Colors.accent : Colors.textDim
                font.pixelSize: 12

                MouseArea {
                    id: ejectMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    onClicked: places.eject(row.index)
                }
            }

            MouseArea {
                id: rowMouse

                anchors.fill: parent
                anchors.rightMargin: row.ejectable ? 24 : 0
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        rowMenu.rowLocation = row.location;
                        rowMenu.rowSection = row.section;
                        rowMenu.popup();
                    } else if (mouse.button === Qt.MiddleButton) {
                        if (row.location !== "" && row.location !== "network:///")
                            root.openInNewTabRequested(row.location);
                    } else if (row.location !== "") {
                        root.navigateRequested(row.location);
                    } else if (row.mountable) {
                        places.mount(row.index);
                    }
                }
            }
        }
    }

    // Nautilus 50's operations indicator: progress lives at the bottom of the
    // sidebar — one row per operation, a pie that fills as it runs beside a
    // live short status ("Copying “name”"). Click for the per-operation
    // popover; the whole thing lingers a few seconds once the queue drains,
    // and pulses the accent when an operation starts.
    Item {
        id: opsArea

        readonly property bool hasOps: FileOperations.operations.length > 0
        property bool linger: false

        onHasOpsChanged: {
            if (hasOps) {
                opsLinger.stop();
                linger = false;
                attention.restart();
            } else {
                linger = true;
                opsLinger.restart();
            }
        }

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 1 // stay clear of the separating hairline
        visible: hasOps || linger || opsPopover.visible
        height: visible ? opsColumn.height + 14 : 0

        Timer {
            id: opsLinger
            interval: 4000
            onTriggered: opsArea.linger = false
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 1
            color: opsPopover.visible ? Colors.selection
                 : opsMouse.containsMouse ? Colors.hover : "transparent"
        }

        // The attention pulse, over the hover wash and under the rows.
        Rectangle {
            id: attentionWash
            anchors.fill: parent
            anchors.topMargin: 1
            color: Colors.accent
            opacity: 0
        }

        SequentialAnimation {
            id: attention
            loops: 2
            NumberAnimation {
                target: attentionWash; property: "opacity"
                from: 0; to: 0.3; duration: 220
            }
            NumberAnimation {
                target: attentionWash; property: "opacity"
                from: 0.3; to: 0; duration: 220
            }
        }

        // The hairline separating the indicator from the places above it.
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Colors.border
        }

        Column {
            id: opsColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            // The drained state: full pie, past-tense line, then gone.
            Row {
                visible: !opsArea.hasOps
                spacing: 8
                OpsPie { fraction: 1; done: true }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Operations complete")
                    color: Colors.textDim
                    font.pixelSize: 12
                }
            }

            Repeater {
                model: FileOperations.operations

                Row {
                    required property var modelData

                    width: opsColumn.width
                    spacing: 8

                    OpsPie {
                        fraction: modelData.state === "running" ? modelData.progress : 0
                        done: false
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 22
                        text: modelData.shortStatus
                        color: modelData.state === "running" ? Colors.text : Colors.textDim
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        MouseArea {
            id: opsMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: opsPopover.visible ? opsPopover.close() : opsPopover.open()
        }

        ToolTip.visible: opsMouse.containsMouse && !opsPopover.visible
        ToolTip.text: qsTr("File operations")
        ToolTip.delay: 600

        // The detail popover, above the indicator: per-operation rows with a
        // progress bar, the current file with byte/rate details and the time
        // estimate, and a per-operation cancel.
        Popup {
            id: opsPopover

            x: 6
            y: -height - 8
            width: 340
            padding: 14
            // Take the keyboard while open, like a GTK popover: without
            // focus, CloseOnEscape never fires and Escape cannot reach it —
            // the popover became unclosable from the keyboard and the window
            // shortcuts stayed half-dead behind it (found by the mouse
            // checklist, 2026-08-09).
            focus: true
            onClosed: root.opsPopoverClosed()
            background: Rectangle {
                color: Colors.chrome
                border.color: Colors.border
                border.width: 1
                radius: Colors.radius
            }

            contentItem: Column {
                spacing: 12

                Text {
                    visible: FileOperations.operations.length === 0
                    text: qsTr("All operations complete")
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                Repeater {
                    model: FileOperations.operations

                    Column {
                        required property var modelData

                        width: 340 - 28
                        spacing: 4

                        Row {
                            width: parent.width
                            spacing: 6

                            Text {
                                width: parent.width - 22
                                text: modelData.state === "queued"
                                      ? modelData.label + qsTr(" — waiting")
                                      : modelData.label
                                color: Colors.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            // Per-operation cancel: interrupts the
                            // running one, drops a queued one.
                            Text {
                                text: "✕"
                                color: cancelOneMouse.containsMouse
                                       ? Colors.text : Colors.textDim
                                font.pixelSize: 12

                                MouseArea {
                                    id: cancelOneMouse
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: FileOperations.cancelOperation(modelData.id)
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Colors.border
                            visible: modelData.state === "running"

                            Rectangle {
                                width: parent.width * modelData.progress
                                height: parent.height
                                radius: 2
                                color: Colors.accent
                            }
                        }

                        // The current file on its own line — a long name must
                        // not elide the numbers off the line below it.
                        Text {
                            visible: (modelData.detail || "") !== ""
                            width: parent.width
                            text: modelData.detail || ""
                            color: Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }

                        // Nautilus's details line: bytes, rate, time.
                        Text {
                            readonly property string line: {
                                const bits = [];
                                if (modelData.transferred) bits.push(modelData.transferred);
                                if (modelData.remaining) bits.push(modelData.remaining);
                                return bits.join(" — ");
                            }
                            visible: line !== ""
                            width: parent.width
                            text: line
                            color: Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // One menu for every row; the click stamps which row it is about.
    Menu {
        id: rowMenu

        property string rowLocation: ""
        property string rowSection: ""

        MenuItem {
            text: qsTr("Open in New Tab")
            enabled: rowMenu.rowLocation !== "" && rowMenu.rowLocation !== "network:///"
            onTriggered: root.openInNewTabRequested(rowMenu.rowLocation)
        }

        MenuItem {
            text: qsTr("Remove Bookmark")
            visible: rowMenu.rowSection === "Bookmarks"
            height: visible ? implicitHeight : 0
            onTriggered: places.removeBookmark(rowMenu.rowLocation)
        }

        MenuItem {
            text: qsTr("Empty Trash…")
            visible: rowMenu.rowLocation === "trash:///"
            height: visible ? implicitHeight : 0
            onTriggered: root.emptyTrashRequested()
        }
    }

    // The hairline that separates the sidebar from the view.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Colors.border
    }
}
