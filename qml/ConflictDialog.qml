import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Asked before an operation starts, once, for the whole set.
//
// "Keep both" is the default and the first button: it is the only choice that
// cannot lose data, and it should be the one a hurried Return keypress picks.
Dialog {
    id: root

    property var conflicting: []

    signal chosen(int policy)

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    closePolicy: Popup.CloseOnEscape

    function askAbout(names) {
        conflicting = names;
        open();
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Keep both")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: { root.chosen(FileOperations.RenameNew); root.close(); }
        }
        Button {
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: { root.chosen(FileOperations.Skip); root.close(); }
        }
        Button {
            text: qsTr("Replace")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: { root.chosen(FileOperations.Replace); root.close(); }
        }
    }

    Column {
        width: parent.width
        spacing: 8

        Text {
            width: parent.width
            text: root.conflicting.length === 1
                  ? qsTr("“%1” already exists here.").arg(root.conflicting[0])
                  : qsTr("%1 items already exist here.").arg(root.conflicting.length)
            color: Colors.text
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: root.conflicting.length > 1
            text: root.conflicting.slice(0, 6).join(", ")
                  + (root.conflicting.length > 6 ? "…" : "")
            color: Colors.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: qsTr("Replacing cannot be undone.")
            color: Colors.textDim
            font.pixelSize: 12
        }
    }
}
