import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// For the actions that cannot be taken back.
Dialog {
    id: root

    property string message: ""
    property string detail: ""
    property string confirmText: "Delete"

    signal confirmed()

    anchors.centerIn: Overlay.overlay
    width: 440
    modal: true
    closePolicy: Popup.CloseOnEscape

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        Button {
            text: root.confirmText
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }

    Column {
        width: parent.width
        spacing: 8

        Text {
            width: parent.width
            text: root.message
            color: Colors.text
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: root.detail !== ""
            text: root.detail
            color: Colors.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    onAccepted: root.confirmed()
}
