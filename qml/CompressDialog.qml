import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Nautilus's "Compress Files and Folders": archive name, compression method
// with its compatibility note, refused while the name is taken or invalid.
// Encrypted ZIP is Nautilus's fourth method: zipcrypt with a password field.
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    closePolicy: Popup.CloseOnEscape

    property var paths: []
    property string directory: ""
    property var existingNames: []
    property int formatIndex: 0

    readonly property var formats: [
        { label: qsTr("ZIP (.zip)"), extension: ".zip",
          note: qsTr("Compatible with all operating systems.") },
        { label: qsTr("TAR (.tar.xz)"), extension: ".tar.xz",
          note: qsTr("Smaller archives but Linux and Mac only.") },
        { label: qsTr("7Z (.7z)"), extension: ".7z",
          note: qsTr("Smaller archives but must be installed on Windows and Mac.") },
        { label: qsTr("Encrypted ZIP (.zip)"), extension: ".zip", encrypted: true,
          note: qsTr("Password-protected. Compatible with all operating systems.") },
    ]

    readonly property bool wantsPassword: formats[formatIndex].encrypted === true

    readonly property string archiveName: nameField.text.trim() + formats[formatIndex].extension
    readonly property string problem: {
        const name = nameField.text.trim();
        if (name === "")
            return "";
        if (name.indexOf("/") >= 0 || name === "." || name === "..")
            return qsTr("File names cannot contain “/”");
        if (existingNames.indexOf(archiveName) >= 0)
            return qsTr("“%1” already exists").arg(archiveName);
        return "";
    }
    readonly property bool ready: nameField.text.trim() !== "" && problem === ""
        && (!wantsPassword || passwordField.text !== "")

    function askAbout(selectedPaths, targetDirectory, suggestedName, names) {
        paths = selectedPaths;
        directory = targetDirectory;
        existingNames = names;
        formatIndex = 0;
        // Checked state is restored by hand: a clicked RadioButton's binding
        // is severed, so a reopened dialog cannot rely on it (the batch
        // rename dialog's lesson).
        for (let i = 0; i < formatRepeater.count; ++i)
            formatRepeater.itemAt(i).checked = (i === 0);
        nameField.text = suggestedName;
        passwordField.text = "";
        open();
        nameField.forceActiveFocus();
        nameField.selectAll();
    }

    function performCompress() {
        if (!ready)
            return;
        FileOperations.compress(paths, directory + "/" + archiveName,
                                wantsPassword ? passwordField.text : "");
        close();
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
        Button {
            text: qsTr("Compress")
            enabled: root.ready
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: root.performCompress()
        }
    }

    Column {
        width: parent.width
        spacing: 12

        Text {
            text: qsTr("Compress Files and Folders")
            color: Colors.text
            font.pixelSize: 15
            font.bold: true
        }

        Column {
            width: parent.width
            spacing: 4

            Text {
                text: qsTr("Archive Name")
                color: Colors.textDim
                font.pixelSize: 12
            }

            Row {
                width: parent.width
                spacing: 6

                TextField {
                    id: nameField
                    width: parent.width - extensionLabel.width - 6
                    color: Colors.text
                    selectByMouse: true
                    placeholderText: qsTr("archive")
                    onAccepted: root.performCompress()
                }

                Text {
                    id: extensionLabel
                    text: root.formats[root.formatIndex].extension
                    color: Colors.textDim
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        Column {
            width: parent.width
            spacing: 2

            Text {
                text: qsTr("Compression Method")
                color: Colors.textDim
                font.pixelSize: 12
            }

            // Radios rather than Nautilus's combo row: three options do not
            // need a dropdown, and the note reads better always visible.
            Repeater {
                id: formatRepeater
                model: root.formats
                RadioButton {
                    required property var modelData
                    required property int index
                    text: modelData.label
                    checked: index === root.formatIndex
                    onToggled: if (checked) root.formatIndex = index
                }
            }

            Text {
                width: parent.width
                text: root.formats[root.formatIndex].note
                color: Colors.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        Column {
            width: parent.width
            spacing: 4
            visible: root.wantsPassword

            Text {
                text: qsTr("Password")
                color: Colors.textDim
                font.pixelSize: 12
            }

            TextField {
                id: passwordField
                width: parent.width
                color: Colors.text
                echoMode: TextInput.Password
                selectByMouse: true
                onAccepted: root.performCompress()
            }
        }

        Text {
            width: parent.width
            visible: root.problem !== ""
            text: root.problem
            color: Colors.error
            font.pixelSize: 12
        }
    }
}
