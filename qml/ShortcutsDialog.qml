import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Nautilus's Keyboard Shortcuts window (Ctrl+?), as one scrollable themed
// list. The data is a static mirror of what Main.qml and Tab.qml actually
// bind — when a binding changes, this list must move with it.
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 560
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 80 : 640)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Keyboard Shortcuts")

    readonly property var groups: [
        { name: qsTr("Windows and tabs"), rows: [
            ["Ctrl+N", qsTr("New window")], ["Ctrl+Shift+W", qsTr("Close window")],
            ["Ctrl+T", qsTr("New tab")], ["Ctrl+W", qsTr("Close tab")],
            ["Ctrl+Tab / Ctrl+PgDn", qsTr("Next tab")], ["Ctrl+Shift+Tab / Ctrl+PgUp", qsTr("Previous tab")],
            ["F3", qsTr("Split view")], ["F6", qsTr("Switch pane")],
            ["F9", qsTr("Toggle sidebar")]] },
        { name: qsTr("Navigation"), rows: [
            ["Alt+Left / Alt+Right", qsTr("Back / forward")], ["Alt+Up", qsTr("Parent folder")],
            ["Alt+Home", qsTr("Home folder")], ["Ctrl+L", qsTr("Edit the location")],
            ["Enter", qsTr("Open the selection")], ["Backspace", qsTr("Parent folder")]] },
        { name: qsTr("View"), rows: [
            ["Ctrl+1 / Ctrl+2", qsTr("List / icon view")], ["Ctrl+H", qsTr("Show hidden files")],
            ["Ctrl++ / Ctrl+-", qsTr("Zoom in / out")], ["Ctrl+0", qsTr("Reset zoom")],
            ["Ctrl+R / F5", qsTr("Reload")]] },
        { name: qsTr("Search"), rows: [
            ["Ctrl+F", qsTr("Search the current folder")],
            ["Ctrl+Shift+F", qsTr("Search file contents")]] },
        { name: qsTr("Files"), rows: [
            ["Ctrl+C / Ctrl+X / Ctrl+V", qsTr("Copy / cut / paste")],
            ["Ctrl+Z", qsTr("Undo")], ["Ctrl+Shift+Z", qsTr("Redo")],
            ["Ctrl+A", qsTr("Select all")],
            ["F2", qsTr("Rename (batch rename on a multi-selection)")],
            ["Delete", qsTr("Move to trash")], ["Shift+Delete", qsTr("Delete permanently")],
            ["Ctrl+Shift+N", qsTr("New folder")], ["Ctrl+D", qsTr("Bookmark this folder")],
            ["Ctrl+I / Alt+Return", qsTr("Properties")]] },
        { name: qsTr("Application"), rows: [
            ["Ctrl+,", qsTr("Preferences")], ["Ctrl+?", qsTr("Keyboard shortcuts")]] }
    ]

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 4

            Repeater {
                model: root.groups

                Column {
                    required property var modelData
                    width: parent.width
                    spacing: 2

                    Text {
                        text: modelData.name
                        color: Colors.text
                        font.pixelSize: 14
                        font.bold: true
                        topPadding: 10
                        bottomPadding: 4
                    }

                    Repeater {
                        model: modelData.rows

                        Row {
                            required property var modelData
                            width: parent.width
                            spacing: 10

                            Text {
                                width: 220
                                text: modelData[0]
                                color: Colors.accent
                                font.pixelSize: 12
                                font.family: "monospace"
                            }

                            Text {
                                text: modelData[1]
                                color: Colors.textDim
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
