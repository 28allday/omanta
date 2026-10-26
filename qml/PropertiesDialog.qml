import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Everything about a selection that the listing has no room for.
//
// Modal, like the app's other dialogs, and it hands the keyboard back on close
// — a dialog that keeps focus leaves the window unnavigable, which is a bug
// this app has already had once.
Dialog {
    id: root

    // What the dialog is currently claiming, hoisted so the window can publish
    // it over D-Bus. A dialog that only a human can read is a dialog only a
    // human can test, and that is how the last two UI bugs here survived.
    readonly property string subjectName: properties.displayName
    readonly property int subjectCount: properties.itemCount
    readonly property real subjectSize: properties.size
    readonly property string subjectMode: properties.modeOctal
    readonly property bool measuring: properties.measuring
    readonly property int currentTab: tabs.currentIndex

    anchors.centerIn: Overlay.overlay
    width: 540
    height: 540
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Not the file's name: the header below already says it, in a larger font
    // and next to its icon. Printing it twice reads like a rendering fault.
    title: qsTr("Properties")

    // Apps the user picked from the full list that the type does not list
    // yet. Cleared per file, so one file's addition does not follow the next.
    property var extraApplications: []

    function show(paths) {
        extraApplications = [];
        properties.paths = paths;
        tabs.currentIndex = 0;
        open();
    }

    function applicationModel() {
        const registered = properties.applications();
        const model = [];
        const seen = {};
        for (let i = 0; i < registered.length; ++i) {
            model.push(registered[i]);
            seen[registered[i].id] = true;
        }
        for (let i = 0; i < extraApplications.length; ++i) {
            const app = extraApplications[i];
            if (!seen[app.id])
                model.push(app);
        }
        return model;
    }

    function selectApplication(id) {
        const model = applicationList.model;
        for (let i = 0; i < model.length; ++i) {
            if (model[i].id === id) {
                applicationList.currentIndex = i;
                applicationList.positionViewAtIndex(i, ListView.Contain);
                return;
            }
        }
        applicationList.currentIndex = -1;
    }

    function refreshApplications(selectId) {
        const keep = selectId !== "" ? selectId
                   : applicationList.currentIndex >= 0
                     ? applicationList.model[applicationList.currentIndex].id : "";
        applicationList.model = applicationModel();
        if (keep !== "")
            selectApplication(keep);
    }

    function addChosenApplication(app) {
        let known = false;
        for (let i = 0; i < extraApplications.length; ++i) {
            if (extraApplications[i].id === app.id)
                known = true;
        }
        if (!known) {
            extraApplications = extraApplications.concat([{
                id: app.id,
                name: app.name,
                iconSource: app.iconSource,
                isDefault: false
            }]);
        }
        refreshApplications(app.id);
    }

    // The dialog's keyboard, wired by hand.
    //
    // A TabBar knows how to handle Left/Right, but only while it has active
    // focus — and inside this dialog it never gets it, whatever focusPolicy it
    // is given. Rather than leave the tabs mouse-only, the keys are bound
    // explicitly. Safe to take globally: while a modal dialog is up, nothing
    // behind it can act on them anyway.
    function selectTab(index) {
        if (tabs.itemAt(index) && tabs.itemAt(index).enabled)
            tabs.currentIndex = index;
    }

    function cycleTab(delta) {
        // Skips over tabs that do not apply to this selection, so Right never
        // lands on a disabled panel.
        for (let index = tabs.currentIndex + delta; index >= 0 && index < tabs.count;
             index += delta) {
            if (tabs.itemAt(index).enabled) {
                tabs.currentIndex = index;
                return;
            }
        }
    }

    Shortcut { sequence: "Right"; enabled: root.opened; onActivated: root.cycleTab(1) }
    Shortcut { sequence: "Left"; enabled: root.opened; onActivated: root.cycleTab(-1) }

    // Direct access too. Alt+number rather than Ctrl+number, which the window
    // itself already uses to switch between list and icon views. One Shortcut
    // each: a single `sequences` list gives no way to tell which of them fired.
    Shortcut { sequence: "Alt+1"; enabled: root.opened; onActivated: root.selectTab(0) }
    Shortcut { sequence: "Alt+2"; enabled: root.opened; onActivated: root.selectTab(1) }
    Shortcut { sequence: "Alt+3"; enabled: root.opened; onActivated: root.selectTab(2) }

    // Closing must stop the tree walk. Leaving it running spends real disk on
    // an answer nobody is looking at any more.
    onClosed: {
        allApplicationsDialog.close();
        properties.paths = [];
    }

    FileProperties {
        id: properties
    }

    // The app launcher's applications, for when the type's own list is missing
    // the one the user actually wants. OK adds that app to the Open With
    // list and selects it, so Set as Default and Open apply to it.
    Dialog {
        id: allApplicationsDialog

        property var allApps: []

        anchors.centerIn: Overlay.overlay
        width: 420
        height: 480
        modal: true
        title: qsTr("Other Application")
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        function openChooser() {
            // Reparent onto the window overlay at the moment it is asked for.
            // Created as a child of Properties, the popup would be clipped to
            // that page; the overlay is not ready when this object is built.
            parent = Overlay.overlay;
            searchField.text = "";
            allApps = properties.allApplications();
            applyFilter();
            open();
            searchField.forceActiveFocus();
        }

        function applyFilter() {
            const query = searchField.text.trim().toLowerCase();
            const shown = query === "" ? allApps : allApps.filter(app =>
                String(app.name).toLowerCase().indexOf(query) !== -1);
            appChooser.model = shown;
            appChooser.currentIndex = shown.length === 1 ? 0 : -1;
        }

        function acceptChosen() {
            if (appChooser.currentIndex < 0)
                return;
            root.addChosenApplication(appChooser.model[appChooser.currentIndex]);
            close();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            TextField {
                id: searchField

                Layout.fillWidth: true
                color: Colors.text
                placeholderText: qsTr("Search applications")
                selectByMouse: true
                onTextChanged: allApplicationsDialog.applyFilter()
                onAccepted: allApplicationsDialog.acceptChosen()
            }

            ListView {
                id: appChooser

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                currentIndex: -1
                model: []

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: appChooser.width
                    height: 32
                    radius: Colors.radius
                    color: index === appChooser.currentIndex ? Colors.selection
                         : chooserMouse.containsMouse ? Colors.hover : "transparent"

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Image {
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                            source: modelData.iconSource
                            sourceSize: Qt.size(20, 20)
                        }

                        Text {
                            textFormat: Text.PlainText
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 28
                            text: modelData.name
                            color: index === appChooser.currentIndex ? Colors.selectionText : Colors.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: chooserMouse

                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: appChooser.currentIndex = index
                        onDoubleClicked: allApplicationsDialog.acceptChosen()
                    }
                }
            }

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: appChooser.count === 0
                text: qsTr("No application matches.")
                color: Colors.textDim
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Cancel")
                    onClicked: allApplicationsDialog.close()
                }

                Button {
                    text: qsTr("OK")
                    enabled: appChooser.currentIndex >= 0
                    onClicked: allApplicationsDialog.acceptChosen()
                }
            }
        }
    }

    // The open-with list is a snapshot, not a bound property — it has to be
    // asked for again whenever the file, or its default application, changes.
    Connections {
        target: properties
        function onInfoChanged() {
            permissions.draft = properties.mode;
            root.refreshApplications("");
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---- header ------------------------------------------------------

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Image {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                source: properties.itemCount > 0
                        ? Colors.tint(properties.iconSource, Colors.accent) : ""
                sourceSize: Qt.size(48, 48)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: properties.itemCount === 1
                          ? properties.displayName
                          : qsTr("%1 items selected").arg(properties.itemCount)
                    color: Colors.text
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideMiddle
                }

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: properties.itemCount === 1 ? properties.typeDescription : ""
                    visible: text !== ""
                    color: Colors.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: properties.errorMessage !== ""
            text: properties.errorMessage
            color: Colors.error
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        TabBar {
            id: tabs

            Layout.fillWidth: true

            TabButton { text: qsTr("Basic") }

            TabButton {
                text: qsTr("Permissions")
                // One set of checkboxes over several files is a way to change
                // the wrong one, so it is only offered for a single item.
                enabled: properties.itemCount === 1 && properties.mode >= 0
            }

            TabButton {
                text: qsTr("Open With")
                enabled: properties.itemCount === 1 && !properties.isDir
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- basic ---------------------------------------------------

            Flickable {
                contentHeight: basicColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: basicColumn

                    width: parent.width
                    spacing: 2

                    PropertyRow {
                        label: qsTr("Type")
                        // The MIME type earns its place on a file, where it
                        // says which application will open it. On a folder it
                        // is just "inode/directory" taking up a line.
                        value: properties.itemCount !== 1 ? ""
                             : properties.isDir || properties.contentType === ""
                               ? properties.typeDescription
                               : properties.typeDescription + " (" + properties.contentType + ")"
                    }

                    PropertyRow {
                        label: qsTr("Link target")
                        value: properties.isSymlink ? properties.symlinkTarget : ""
                    }

                    PropertyRow {
                        label: properties.isDir || properties.itemCount > 1
                               ? qsTr("Contents") : qsTr("Size")
                        // The number climbs while a folder is being walked, so
                        // it says so rather than looking like a settled answer.
                        value: {
                            if (properties.itemCount === 0)
                                return "";
                            const total = Platform.formatSize(properties.size)
                                        + (properties.measuring ? "…" : "");
                            if (!properties.isDir && properties.itemCount === 1)
                                return total;
                            const files = properties.fileCount === 1
                                        ? qsTr("1 file")
                                        : qsTr("%1 files").arg(properties.fileCount);
                            const folders = properties.folderCount === 1
                                          ? qsTr("1 folder")
                                          : qsTr("%1 folders").arg(properties.folderCount);
                            return files + ", " + folders + ", " + total;
                        }
                    }

                    PropertyRow {
                        label: qsTr("Size on disk")
                        value: !properties.isDir && properties.itemCount === 1
                               ? Platform.formatSize(properties.sizeOnDisk) : ""
                    }

                    PropertyRow {
                        label: qsTr("Location")
                        value: properties.location
                    }

                    PropertyRow {
                        label: qsTr("Modified")
                        value: properties.itemCount === 1
                               ? Platform.formatTimestamp(properties.modified) : ""
                    }

                    PropertyRow {
                        label: qsTr("Accessed")
                        value: properties.itemCount === 1
                               ? Platform.formatTimestamp(properties.accessed) : ""
                    }

                    PropertyRow {
                        label: qsTr("Created")
                        value: properties.itemCount === 1
                               ? Platform.formatTimestamp(properties.created) : ""
                    }

                    PropertyRow {
                        label: qsTr("Owner")
                        value: properties.itemCount === 1 ? properties.owner : ""
                    }

                    PropertyRow {
                        label: qsTr("Group")
                        value: properties.itemCount === 1 ? properties.group : ""
                    }

                    PropertyRow {
                        label: qsTr("Free space")
                        value: properties.filesystemSize <= 0 ? ""
                             : qsTr("%1 free of %2 (%3)")
                               .arg(Platform.formatSize(properties.filesystemFree))
                               .arg(Platform.formatSize(properties.filesystemSize))
                               .arg(properties.filesystemType)
                    }
                }
            }

            // ---- permissions ---------------------------------------------

            ColumnLayout {
                id: permissions

                // What the checkboxes currently say, which is only written to
                // disk when Apply is pressed. A half-ticked permission set is
                // not one anybody meant.
                property int draft: properties.mode

                spacing: 12

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 20
                    rowSpacing: 4

                    Text { textFormat: Text.PlainText; text: ""; Layout.preferredWidth: 70 }
                    Text { textFormat: Text.PlainText; text: qsTr("Read"); color: Colors.textDim; font.pixelSize: 12 }
                    Text { textFormat: Text.PlainText; text: qsTr("Write"); color: Colors.textDim; font.pixelSize: 12 }
                    Text {
                        textFormat: Text.PlainText
                        text: properties.isDir ? qsTr("Enter") : qsTr("Execute")
                        color: Colors.textDim
                        font.pixelSize: 12
                    }

                    // Written out rather than generated: a Repeater cannot emit
                    // four grid cells per model row without a wrapper Item,
                    // and the wrapper is what breaks the column alignment.
                    Text { textFormat: Text.PlainText; text: qsTr("Owner"); color: Colors.text; font.pixelSize: 12 }
                    PermissionBox { bit: 0o400 }
                    PermissionBox { bit: 0o200 }
                    PermissionBox { bit: 0o100 }

                    Text { textFormat: Text.PlainText; text: qsTr("Group"); color: Colors.text; font.pixelSize: 12 }
                    PermissionBox { bit: 0o040 }
                    PermissionBox { bit: 0o020 }
                    PermissionBox { bit: 0o010 }

                    Text { textFormat: Text.PlainText; text: qsTr("Others"); color: Colors.text; font.pixelSize: 12 }
                    PermissionBox { bit: 0o004 }
                    PermissionBox { bit: 0o002 }
                    PermissionBox { bit: 0o001 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("On disk: %1  %2").arg(properties.modeText)
                                                     .arg(properties.modeOctal)
                        color: Colors.textDim
                        font.family: "monospace"
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Apply")
                        enabled: properties.canChangeMode
                                 && permissions.draft !== properties.mode
                        onClicked: properties.applyMode(permissions.draft)
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: !properties.canChangeMode && properties.itemCount === 1
                    text: qsTr("Only the owner of a file can change its permissions.")
                    color: Colors.textDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }
            }

            // ---- open with -----------------------------------------------

            ColumnLayout {
                spacing: 8

                ListView {
                    id: applicationList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    currentIndex: -1
                    model: []

                    delegate: Rectangle {
                        required property int index
                        required property var modelData

                        width: applicationList.width
                        height: 32
                        radius: Colors.radius
                        color: index === applicationList.currentIndex ? Colors.selection
                             : applicationMouse.containsMouse ? Colors.hover : "transparent"

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Image {
                                width: 20
                                height: 20
                                anchors.verticalCenter: parent.verticalCenter
                                source: modelData.iconSource
                                sourceSize: Qt.size(20, 20)
                            }

                            Text {
                                textFormat: Text.PlainText
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name
                                      + (modelData.isDefault ? " — " + qsTr("default") : "")
                                color: index === applicationList.currentIndex ? Colors.selectionText : Colors.text
                                font.pixelSize: 12
                            }
                        }

                        MouseArea {
                            id: applicationMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: applicationList.currentIndex = index
                            onDoubleClicked: properties.launchWith(modelData.id)
                        }
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: applicationList.count === 0
                    text: qsTr("No application is registered for this type.")
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: qsTr("Set as Default")
                        enabled: applicationList.currentIndex >= 0
                        onClicked: {
                            const chosen = applicationList.model[applicationList.currentIndex];
                            if (properties.setDefaultApplication(chosen.id)) {
                                // The list carries the "default" marker, so it
                                // has to be asked again rather than patched.
                                root.refreshApplications(chosen.id);
                            }
                        }
                    }

                    Button {
                        text: qsTr("Open")
                        enabled: applicationList.currentIndex >= 0
                        onClicked: properties.launchWith(
                                       applicationList.model[applicationList.currentIndex].id)
                    }

                    Button {
                        text: qsTr("Other")
                        enabled: properties.loaded && properties.itemCount === 1
                                 && !properties.isDir
                        onClicked: allApplicationsDialog.openChooser()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    // ---- small pieces -----------------------------------------------------

    // One label/value line. Rows with nothing to say hide themselves, which is
    // what keeps a folder's panel from showing four empty fields.
    component PropertyRow: RowLayout {
        id: propertyRow

        property string label: ""
        property string value: ""

        Layout.fillWidth: true
        visible: value !== ""
        spacing: 10

        Text {
            textFormat: Text.PlainText
            Layout.preferredWidth: 100
            Layout.alignment: Qt.AlignTop
            topPadding: 3
            text: propertyRow.label
            color: Colors.textDim
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            topPadding: 3
            bottomPadding: 3
            text: propertyRow.value
            color: Colors.text
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }
    }

    component PermissionBox: CheckBox {
        property int bit: 0

        enabled: properties.canChangeMode
        checked: (permissions.draft & bit) !== 0
        onToggled: permissions.draft = checked ? (permissions.draft | bit)
                                               : (permissions.draft & ~bit)
    }
}
