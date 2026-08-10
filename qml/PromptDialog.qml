import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Asks for one line of text — a new folder's name, a file's new name.
Dialog {
    id: root

    property string prompt: ""
    property string initialText: ""
    // Selecting the stem but not the extension is what every file manager does,
    // because renaming almost never means changing ".jpg".
    property bool selectStem: false
    // Passwords: dots instead of characters.
    property bool secret: false

    signal accepted_(string text)

    anchors.centerIn: Overlay.overlay
    width: 420
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    function ask() {
        field.text = root.initialText;
        if (root.selectStem) {
            const dot = root.initialText.lastIndexOf(".");
            if (dot > 0)
                field.select(0, dot);
            else
                field.selectAll();
        } else {
            field.selectAll();
        }
        open();
        field.forceActiveFocus();
    }

    Column {
        width: parent.width
        spacing: 10

        Text {
            text: root.prompt
            color: Colors.text
            font.pixelSize: 13
        }

        TextField {
            id: field

            width: parent.width
            color: Colors.text
            selectByMouse: true
            echoMode: root.secret ? TextInput.Password : TextInput.Normal
            onAccepted: if (text.length > 0) root.accept()
        }
    }

    onAccepted: {
        // A password is passed through untouched — trimming one is corruption.
        const value = root.secret ? field.text : field.text.trim();
        if (value.length > 0)
            root.accepted_(value);
    }
}
