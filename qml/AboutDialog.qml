import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// About Files — the hamburger menu's last entry, omacalc-flat.
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 360
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Held by a property, not the default one: that would hand it to the
    // contentItem (a ScrollView here and there) before it lifts itself out.
    readonly property Item closeButton: DialogCloseButton { dialog: root }

    Column {
        width: parent.width
        spacing: 6
        topPadding: 10
        bottomPadding: 10

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: Colors.tint("image://fileicon/folder", Colors.accent)
            sourceSize: Qt.size(64, 64)
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Files")
            color: Colors.text
            font.pixelSize: 20
            font.bold: true
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: "omanta " + Qt.application.version
            color: Colors.textDim
            font.pixelSize: 12
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("A native file manager for Omarchy.")
            color: Colors.textDim
            font.pixelSize: 12
        }
    }
}
