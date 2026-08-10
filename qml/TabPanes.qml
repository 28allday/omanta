import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// One tab slot: a single Tab, or two side by side while split view (F3) is
// on. The window's chrome talks to `activePane` — whichever pane last took a
// press or was handed the keyboard with F6 — so the toolbar, path bar, status
// line, shortcuts and menus all follow the split without knowing about it.
FocusScope {
    id: slot

    property string initialPath: ""
    property string initialSelection: ""

    property bool split: false
    property Item activePane: firstCell.tab
    readonly property int activePaneIndex: split && secondLoader.item
                                           && activePane === secondLoader.item.tab ? 1 : 0

    signal contextMenuRequested()
    signal mountNeeded(string location)
    signal transferRequested(var sources, string destination, bool isMove)

    function setActive(pane) {
        if (activePane !== pane)
            activePane = pane;
    }

    function toggleSplit() {
        if (!split) {
            // The second pane opens where the user is now — a snapshot, not a
            // binding: from here the two navigate independently.
            secondLoader.spawnPath = activePane.path;
            split = true;
        } else {
            activePane = firstCell.tab;
            split = false;
        }
        activePane.forceActiveFocus();
    }

    function cyclePane() {
        if (!split || !secondLoader.item)
            return;
        activePane = activePane === firstCell.tab ? secondLoader.item.tab
                                                  : firstCell.tab;
        activePane.forceActiveFocus();
    }

    // A pane: the Tab plus, while split, a slim header naming its folder.
    // With one shared path bar the headers are what tell the panes apart —
    // the accent underline marks the one the chrome acts on.
    component PaneCell: FocusScope {
        id: cell

        property alias tab: tab
        property string startPath: ""
        property string startSelection: ""
        readonly property bool isActive: slot.activePane === tab

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 26 : 0
                visible: slot.split
                color: Colors.chrome

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: cell.tab.title
                    color: cell.isActive ? Colors.text : Colors.textDim
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: cell.isActive ? 2 : 1
                    color: cell.isActive ? Colors.accent : Colors.border
                }
            }

            Tab {
                id: tab

                Layout.fillWidth: true
                Layout.fillHeight: true
                focus: true
                path: cell.startPath
                pendingSelection: cell.startSelection
                onContextMenuRequested: slot.contextMenuRequested()
                onMountNeeded: location => slot.mountNeeded(location)
                onTransferRequested: (sources, destination, isMove) =>
                    slot.transferRequested(sources, destination, isMove)
            }
        }

        // Any press in a pane makes it the one the chrome acts on, before
        // whatever was pressed handles the event. Declined presses fall
        // through untouched — clicks, drags and the rubber band never see
        // this MouseArea (the same overlay pattern as the rubber band).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            z: 10
            onPressed: mouse => {
                slot.setActive(cell.tab);
                cell.tab.forceActiveFocus();
                mouse.accepted = false;
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            id: splitHandle
            implicitWidth: 5
            color: Colors.chrome

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: splitHandle.SplitView.pressed ? 2 : 1
                height: parent.height
                color: splitHandle.SplitView.pressed || splitHandle.SplitView.hovered
                       ? Colors.accent : Colors.border
            }
        }

        PaneCell {
            id: firstCell

            SplitView.fillWidth: true
            SplitView.minimumWidth: 220
            focus: true
            startPath: slot.initialPath
            startSelection: slot.initialSelection
        }

        Loader {
            id: secondLoader

            property string spawnPath: ""

            SplitView.preferredWidth: slot.width / 2
            SplitView.minimumWidth: 220
            active: slot.split
            visible: slot.split
            sourceComponent: PaneCell {
                startPath: secondLoader.spawnPath
            }
        }
    }
}
