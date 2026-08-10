import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Nautilus's batch rename, F2 on a multi-selection: a template with
// automatic numbering, or find-and-replace over the full name, previewed
// live and refused while any resulting name conflicts.
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 560
    modal: true
    closePolicy: Popup.CloseOnEscape

    property int count: 0

    BatchRenamer { id: renamer }

    function askAbout(items, existingNames) {
        count = items.length;
        templateButton.checked = true;
        renamer.mode = BatchRenamer.Template;
        renamer.numberingOrder = BatchRenamer.NameAscending;
        templateField.text = "[Original file name]";
        findField.text = "";
        replaceField.text = "";
        renamer.setSelection(items, existingNames);
        open();
        templateField.forceActiveFocus();
        templateField.cursorPosition = templateField.text.length;
    }

    // Enter anywhere in the fields is Rename, as Nautilus's entries
    // activate the default button.
    function performRename() {
        if (!renamer.canRename)
            return;
        FileOperations.batchRename(renamer.sourcePaths(), renamer.newNames());
        close();
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
        Button {
            text: qsTr("Rename")
            enabled: renamer.canRename
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: root.performRename()
        }
    }

    Column {
        width: parent.width
        spacing: 12

        Text {
            text: qsTr("Rename %1 Files").arg(root.count)
            color: Colors.text
            font.pixelSize: 15
            font.bold: true
        }

        // The two modes, as Nautilus words them. Checked state is set by
        // hand in askAbout() and driven by onToggled — a binding here would
        // be severed by the first click.
        Row {
            spacing: 18

            RadioButton {
                id: templateButton
                text: qsTr("Rename using a template")
                onToggled: if (checked) renamer.mode = BatchRenamer.Template
            }
            RadioButton {
                id: replaceButton
                text: qsTr("Find and replace text")
                onToggled: if (checked) renamer.mode = BatchRenamer.FindReplace
            }
        }

        // Template mode: the format field plus Nautilus's "+ Add" tag menu.
        Column {
            width: parent.width
            visible: renamer.mode === BatchRenamer.Template
            spacing: 8

            Row {
                width: parent.width
                spacing: 6

                TextField {
                    id: templateField
                    width: parent.width - addButton.width - 6
                    color: Colors.text
                    selectByMouse: true
                    onTextChanged: renamer.templateText = text
                    onAccepted: root.performRename()
                }

                Button {
                    id: addButton
                    text: qsTr("+ Add")
                    onClicked: addMenu.popup(addButton, 0, addButton.height)

                    Menu {
                        id: addMenu

                        // Inserting at the cursor rather than appending is
                        // what makes "prefix [1, 2, 3] suffix" typeable.
                        function insert(tag) {
                            const at = templateField.cursorPosition;
                            templateField.text = renamer.insertTag(templateField.text, at, tag);
                            templateField.cursorPosition = at + tag.length;
                            templateField.forceActiveFocus();
                        }

                        MenuItem {
                            text: qsTr("1, 2, 3, 4")
                            onTriggered: addMenu.insert("[1, 2, 3]")
                        }
                        MenuItem {
                            text: qsTr("01, 02, 03, 04")
                            onTriggered: addMenu.insert("[01, 02, 03]")
                        }
                        MenuItem {
                            text: qsTr("001, 002, 003, 004")
                            onTriggered: addMenu.insert("[001, 002, 003]")
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Original File Name")
                            onTriggered: addMenu.insert("[Original file name]")
                        }
                    }
                }
            }

            Row {
                spacing: 10
                visible: renamer.hasNumbering

                Text {
                    text: qsTr("Automatic Numbering Order")
                    color: Colors.textDim
                    font.pixelSize: 12
                    anchors.verticalCenter: parent.verticalCenter
                }

                Button {
                    id: orderButton

                    readonly property var labels: [
                        qsTr("Original Name (Ascending)"),
                        qsTr("Original Name (Descending)"),
                        qsTr("First Modified"),
                        qsTr("Last Modified"),
                    ]

                    text: labels[renamer.numberingOrder] + "  ▾"
                    onClicked: orderMenu.popup(orderButton, 0, orderButton.height)

                    Menu {
                        id: orderMenu

                        Repeater {
                            model: orderButton.labels
                            MenuItem {
                                required property int index
                                required property string modelData
                                text: modelData
                                onTriggered: renamer.numberingOrder = index
                            }
                        }
                    }
                }
            }
        }

        // Find/replace mode. Unlike the template, this sees the extension.
        Grid {
            width: parent.width
            visible: renamer.mode === BatchRenamer.FindReplace
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            verticalItemAlignment: Grid.AlignVCenter

            Text {
                text: qsTr("Existing Text")
                color: Colors.textDim
                font.pixelSize: 12
            }
            TextField {
                id: findField
                width: root.width - 160
                color: Colors.text
                selectByMouse: true
                onTextChanged: renamer.findText = text
                onAccepted: root.performRename()
            }

            Text {
                text: qsTr("Replace With")
                color: Colors.textDim
                font.pixelSize: 12
            }
            TextField {
                id: replaceField
                width: root.width - 160
                color: Colors.text
                selectByMouse: true
                onTextChanged: renamer.replaceText = text
                onAccepted: root.performRename()
            }
        }

        // The live preview: every file, old name → new name, conflicts in
        // the theme's error red.
        Rectangle {
            width: parent.width
            height: 220
            color: Colors.chrome
            border.color: Colors.border
            border.width: 1
            radius: 4

            ListView {
                id: previewList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: renamer.preview
                ScrollBar.vertical: ScrollBar {}

                delegate: Row {
                    required property var modelData
                    width: previewList.width
                    height: 24
                    spacing: 8

                    Text {
                        width: (parent.width - 40) / 2
                        text: parent.modelData.oldName
                        color: Colors.textDim
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "→"
                        color: Colors.textDim
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        width: (parent.width - 40) / 2
                        text: parent.modelData.newName
                        color: parent.modelData.conflict ? Colors.error : Colors.text
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        Text {
            width: parent.width
            visible: renamer.problem !== ""
            text: renamer.problem
            color: Colors.error
            font.pixelSize: 12
        }
    }
}
