import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Breadcrumbs, with Ctrl+L flipping to a plain editable path. Both halves are
// always present; only one is visible, so focus handling stays simple.
FocusScope {
    id: root

    property string path: ""
    property bool editing: false

    signal navigateRequested(string target)
    // The kebab at the pill's right end — current-folder actions.
    signal menuRequested()

    // The leading glyph names the kind of place, Nautilus-50-style.
    readonly property string placeIcon: {
        const p = root.path;
        if (p.indexOf("trash:") === 0) return "user-trash";
        if (p.indexOf("recent:") === 0) return "document-open-recent";
        if (p.indexOf("starred:") === 0) return "starred";
        if (p.indexOf("://") >= 0) return "network-server";
        const home = Platform.homePath();
        if (p === home || p.indexOf(home + "/") === 0) return "user-home";
        return "drive-harddisk";
    }

    implicitHeight: 32

    function beginEditing() {
        editing = true;
        editor.text = root.path;
        editor.selectAll();
        editor.forceActiveFocus();
    }

    function cancelEditing() {
        editing = false;
        crumbFlick.forceActiveFocus();
    }

    Rectangle {
        anchors.fill: parent
        radius: Colors.radius
        color: Colors.window
        border.color: root.editing ? Colors.accent : Colors.border
        border.width: 1
    }

    Image {
        id: placeGlyph

        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        sourceSize: Qt.size(28, 28)
        visible: !root.editing
        source: Colors.tint("image://fileicon/" + root.placeIcon, Colors.text)
    }

    Flickable {
        id: crumbFlick

        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        visible: !root.editing
        clip: true
        contentWidth: crumbRow.width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        // Keep the deepest crumb in view: that is the one the user is in.
        onContentWidthChanged: contentX = Math.max(0, contentWidth - width)

        Row {
            id: crumbRow
            height: crumbFlick.height
            spacing: 0

            Repeater {
                id: crumbs

                model: Platform.pathCrumbs(root.path)

                delegate: Row {
                    required property var modelData
                    required property int index

                    height: crumbRow.height

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: index > 0
                        text: "/"
                        color: Colors.textDim
                        font.pixelSize: 13
                        leftPadding: 2
                        rightPadding: 2
                    }

                    Rectangle {
                        height: 24
                        width: crumbLabel.width + 14
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 4
                        color: crumbMouse.containsMouse ? Colors.hover : "transparent"

                        Text {
                            id: crumbLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            // The Repeater already knows how many crumbs there
                            // are; asking Platform again would re-walk the whole
                            // path once per crumb, on every re-evaluation.
                            color: index === crumbs.count - 1 ? Colors.text : Colors.textDim
                            font.pixelSize: 13
                        }

                        MouseArea {
                            id: crumbMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.navigateRequested(modelData.path)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: kebab

        anchors.right: parent.right
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        width: 22
        height: 24
        radius: 4
        visible: !root.editing
        color: kebabMouse.containsMouse ? Colors.hover : "transparent"

        Text {
            anchors.centerIn: parent
            text: "\u22ee"
            color: Colors.text
            font.pixelSize: 14
        }

        MouseArea {
            id: kebabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.menuRequested()
        }
    }

    TextField {
        id: editor

        anchors.fill: parent
        visible: root.editing
        color: Colors.text
        font.pixelSize: 13
        leftPadding: 10
        background: null
        selectByMouse: true

        onAccepted: {
            root.editing = false;
            // "~/Documents" and "../src" are what people actually type.
            root.navigateRequested(Platform.resolvePath(text, root.path));
        }

        Keys.onEscapePressed: root.cancelEditing()
        onActiveFocusChanged: if (!activeFocus && root.editing) root.cancelEditing()
    }
}
