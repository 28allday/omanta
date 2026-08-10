import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// A small themed directory chooser — "Extract to…" needs one, and omanta
// picking folders with a GTK portal dialog would be absurd. Navigate by
// double-click (or Enter), Backspace/the ⬆ button go up; Select takes the
// highlighted folder, or the folder being viewed when nothing is.
Dialog {
    id: root

    property string acceptLabel: qsTr("Select")
    // What the choice is for, shown under the title: "Extract 2 archives to:"
    property string prompt: ""

    signal picked(string path)

    anchors.centerIn: Overlay.overlay
    width: 480
    height: 460
    modal: true
    closePolicy: Popup.CloseOnEscape

    function askFor(startPath, promptText) {
        prompt = promptText;
        picker.path = startPath;
        list.currentIndex = -1;
        open();
        list.forceActiveFocus();
    }

    readonly property string chosenPath: list.currentIndex >= 0
        ? String(proxy.valueAt(list.currentIndex, "filePath"))
        : picker.path

    function descend() {
        if (list.currentIndex < 0)
            return;
        picker.path = String(proxy.valueAt(list.currentIndex, "filePath"));
        list.currentIndex = -1;
    }

    function up() {
        const parent = Platform.parentPath(picker.path);
        if (parent !== "" && parent !== picker.path) {
            picker.path = parent;
            list.currentIndex = -1;
        }
    }

    DirectoryModel {
        id: picker
    }

    FileSortFilterModel {
        id: proxy
        sourceModel: picker
        foldersOnly: true
        foldersFirst: true
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
        Button {
            text: root.acceptLabel
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                root.picked(root.chosenPath);
                root.close();
            }
        }
    }

    contentItem: Column {
        spacing: 8

        Text {
            visible: root.prompt !== ""
            width: parent.width
            text: root.prompt
            color: Colors.text
            font.pixelSize: 13
            elide: Text.ElideMiddle
        }

        Row {
            width: parent.width
            spacing: 8

            ToolbarButton {
                symbol: "⬆"
                tip: qsTr("Parent folder")
                onTriggered: root.up()
            }

            Text {
                width: parent.width - 40
                anchors.verticalCenter: parent.verticalCenter
                text: picker.path
                color: Colors.textDim
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }

        Rectangle {
            width: parent.width
            height: parent.height - y
            color: "transparent"
            border.color: Colors.border
            border.width: 1
            radius: Colors.radius

            ListView {
                id: list

                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: proxy
                currentIndex: -1
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds

                Keys.onReturnPressed: root.descend()
                Keys.onEnterPressed: root.descend()
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Backspace) {
                        root.up();
                        event.accepted = true;
                    }
                }

                Text {
                    visible: list.count === 0 && !picker.loading
                    anchors.centerIn: parent
                    text: qsTr("No folders here")
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                delegate: Rectangle {
                    required property int index
                    required property string displayName
                    required property string iconSource

                    width: list.width
                    height: 28
                    radius: Colors.radius
                    color: list.currentIndex === index ? Colors.selection
                         : pickMouse.containsMouse ? Colors.hover : "transparent"

                    Image {
                        id: rowIcon
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        source: iconSource
                        sourceSize: Qt.size(16, 16)
                    }

                    Text {
                        anchors.left: rowIcon.right
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: displayName
                        color: list.currentIndex === index ? Colors.selectionText
                                                           : Colors.text
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: pickMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: list.currentIndex = index
                        onDoubleClicked: {
                            list.currentIndex = index;
                            root.descend();
                        }
                    }
                }
            }
        }
    }
}
