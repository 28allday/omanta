import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Nautilus's Preferences dialog, over the Settings store. Every Nautilus
// row now has its feature; nothing is deliberately absent.
//
// Control state is sampled in onAboutToShow rather than bound: interacting
// with a checkable control writes `checked`/`currentIndex`, which would sever
// a binding on first use (fifth appearance of this pattern).
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 560
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 80 : 640)
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Preferences")
    padding: 24
    // Keep the cards centred while leaving room for the bar in the margin.
    leftPadding: rightPadding
    rightPadding: Math.max(24, scroller.ScrollBar.vertical.width + 12)
    topPadding: 12

    readonly property int controlWidth: 190

    background: Rectangle {
        color: Colors.chrome
        border.color: Colors.border
        border.width: 1
        radius: Colors.radius
    }

    header: Label {
        text: root.title
        textFormat: Text.PlainText
        color: Colors.text
        font.pixelSize: 16
        font.bold: true
        leftPadding: root.leftPadding
        rightPadding: root.rightPadding
        topPadding: 18
    }

    onAboutToShow: {
        syncFromSettings();
        // The Toggle menu or a terminal may have switched it meanwhile.
        DefaultFileManager.refresh();
        // A dialog that reopens mid-scroll looks broken.
        scroller.contentItem.contentY = 0;
    }

    function syncFromSettings() {
        foldersFirstSwitch.checked = Settings.sortFoldersFirst;
        clickCombo.currentIndex = Settings.clickPolicy === "single" ? 1 : 0;
        treeViewSwitch.checked = Settings.useTreeView;
        createLinkSwitch.checked = Settings.showCreateLink;
        deletePermanentlySwitch.checked = Settings.showDeletePermanently;
        searchCombo.currentIndex = policyIndex(Settings.searchInSubfolders);
        thumbnailsCombo.currentIndex = policyIndex(Settings.showThumbnails);
        itemCountsCombo.currentIndex = policyIndex(Settings.showDirectoryItemCounts);
        dateSimple.checked = Settings.dateTimeFormat !== "detailed";
        dateDetailed.checked = Settings.dateTimeFormat === "detailed";
        const captions = Settings.iconCaptions;
        captionFirst.currentIndex = captionIndex(captions[0]);
        captionSecond.currentIndex = captionIndex(captions[1]);
        captionThird.currentIndex = captionIndex(captions[2]);
        opacitySlider.value = Settings.backgroundOpacity;
        syncFileManager();
    }

    // Imperative like the rest: a Switch's own toggle breaks a binding, and
    // the truth arrives later, from the switcher's status.
    function syncFileManager() {
        defaultSwitch.checked = DefaultFileManager.isDefault;
        toggleMenuSwitch.checked = DefaultFileManager.menuInstalled;
    }

    Connections {
        target: DefaultFileManager
        function onStatusChanged() { root.syncFileManager(); }
    }

    // Caption combos share one value/label order; "none" leads as default.
    readonly property var captionValues: ["none", "size", "type", "owner", "group",
                                          "permissions", "modified", "created", "accessed"]
    readonly property var captionLabels: [qsTr("None"), qsTr("Size"), qsTr("Type"),
                                          qsTr("Owner"), qsTr("Group"), qsTr("Permissions"),
                                          qsTr("Modified"), qsTr("Created"), qsTr("Accessed")]
    function captionIndex(value) {
        const at = captionValues.indexOf(value);
        return at >= 0 ? at : 0;
    }
    function applyCaptions() {
        Settings.iconCaptions = [captionValues[captionFirst.currentIndex],
                                 captionValues[captionSecond.currentIndex],
                                 captionValues[captionThird.currentIndex]];
    }

    // The three-way performance policies share one value order.
    readonly property var policyValues: ["local-only", "always", "never"]
    readonly property var policyLabels: [qsTr("On This Device Only"), qsTr("All Locations"), qsTr("Never")]
    function policyIndex(value) {
        const at = policyValues.indexOf(value);
        return at >= 0 ? at : 0;
    }

    // A settings row: label left, control right, on the theme's card colour.
    component PrefRow: Rectangle {
        default property alias content: rowLayout.data
        property string label: ""

        width: parent.width
        height: 52
        radius: Colors.radius
        // The view tone on the chrome dialog, same as the location pill —
        // chrome-on-chrome made the cards invisible.
        color: Colors.window
        border.color: Colors.border
        border.width: 1

        RowLayout {
            id: rowLayout
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: label
                color: Colors.text
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }
    }

    component PrefComboBox: ComboBox {
        Layout.minimumWidth: root.controlWidth
        Layout.preferredWidth: root.controlWidth
        Layout.maximumWidth: root.controlWidth
        implicitHeight: 32
        font.pixelSize: 13
    }

    component PrefSwitch: Switch {
        // Align the visible indicator with the dropdowns' right edge.
        padding: 0
        leftPadding: 12
        spacing: 0
        implicitHeight: 32
    }

    component SectionTitle: Text {
        width: parent.width
        textFormat: Text.PlainText
        color: Colors.text
        font.pixelSize: 14
        font.bold: true
        topPadding: 16
    }

    component SectionCaption: Text {
        width: parent.width
        textFormat: Text.PlainText
        color: Colors.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        bottomPadding: 4
    }

    contentItem: ScrollView {
        id: scroller

        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        // Place the bar outside the clipped content, within the dialog's
        // right margin. The cards then have equal space on both sides.
        ScrollBar.vertical.parent: scroller.parent
        ScrollBar.vertical.x: scroller.x + scroller.width + 6
        ScrollBar.vertical.y: scroller.y
        ScrollBar.vertical.height: scroller.height

        Column {
            width: scroller.availableWidth
            spacing: 8

            SectionTitle { text: qsTr("General"); topPadding: 0 }

            PrefRow {
                label: qsTr("Sort Folders Before Files")
                PrefSwitch {
                    id: foldersFirstSwitch
                    onToggled: Settings.sortFoldersFirst = checked
                }
            }

            PrefRow {
                label: qsTr("Action to Open Items")
                PrefComboBox {
                    id: clickCombo
                    model: [qsTr("Double-Click"), qsTr("Single-Click")]
                    onActivated: Settings.clickPolicy = currentIndex === 1 ? "single" : "double"
                }
            }

            PrefRow {
                label: qsTr("Expandable Folders in List View")
                PrefSwitch {
                    id: treeViewSwitch
                    onToggled: Settings.useTreeView = checked
                }
            }

            SectionTitle {
                visible: DefaultFileManager.available
                text: qsTr("Default File Manager")
            }
            SectionCaption {
                visible: DefaultFileManager.available
                text: DefaultFileManager.lastError !== ""
                      ? DefaultFileManager.lastError
                      : qsTr("Open folders, downloads and Super+Shift+F in Omanta instead of Nautilus. Switch back at any time.")
                color: DefaultFileManager.lastError !== "" ? Colors.error : Colors.textDim
            }

            PrefRow {
                visible: DefaultFileManager.available
                label: qsTr("Use Omanta as the Default")
                PrefSwitch {
                    id: defaultSwitch
                    enabled: DefaultFileManager.known && !DefaultFileManager.busy
                    onToggled: DefaultFileManager.setDefault(checked)
                }
            }

            PrefRow {
                visible: DefaultFileManager.available && DefaultFileManager.omarchy
                label: qsTr("Show Switch in Omarchy Toggle Menu")
                PrefSwitch {
                    id: toggleMenuSwitch
                    enabled: DefaultFileManager.known && !DefaultFileManager.busy
                    onToggled: DefaultFileManager.setMenuInstalled(checked)
                }
            }

            SectionTitle { text: qsTr("Optional Context Menu Actions") }
            SectionCaption {
                text: qsTr("Show more actions in the menus. Keyboard shortcuts can be used even if the actions are not shown.")
            }

            PrefRow {
                label: qsTr("Create Link")
                PrefSwitch {
                    id: createLinkSwitch
                    onToggled: Settings.showCreateLink = checked
                }
            }

            PrefRow {
                label: qsTr("Delete Permanently")
                PrefSwitch {
                    id: deletePermanentlySwitch
                    onToggled: Settings.showDeletePermanently = checked
                }
            }

            SectionTitle { text: qsTr("Performance") }
            SectionCaption {
                text: qsTr("These features may cause slowdowns and excess network usage, especially when browsing files outside this device, such as on a remote server.")
            }

            PrefRow {
                label: qsTr("Search in Subfolders")
                PrefComboBox {
                    id: searchCombo
                    model: root.policyLabels
                    onActivated: Settings.searchInSubfolders = root.policyValues[currentIndex]
                }
            }

            PrefRow {
                label: qsTr("Show Thumbnails")
                PrefComboBox {
                    id: thumbnailsCombo
                    model: root.policyLabels
                    onActivated: Settings.showThumbnails = root.policyValues[currentIndex]
                }
            }

            PrefRow {
                label: qsTr("Count Number of Files in Folders")
                PrefComboBox {
                    id: itemCountsCombo
                    model: root.policyLabels
                    onActivated: Settings.showDirectoryItemCounts = root.policyValues[currentIndex]
                }
            }

            SectionTitle { text: qsTr("Icon View Captions") }
            SectionCaption {
                text: qsTr("Add information to be displayed beneath file and folder names. More information will appear when zooming closer.")
            }

            PrefRow {
                label: qsTr("First")
                PrefComboBox {
                    id: captionFirst
                    model: root.captionLabels
                    onActivated: root.applyCaptions()
                }
            }

            PrefRow {
                label: qsTr("Second")
                PrefComboBox {
                    id: captionSecond
                    model: root.captionLabels
                    onActivated: root.applyCaptions()
                }
            }

            PrefRow {
                label: qsTr("Third")
                PrefComboBox {
                    id: captionThird
                    model: root.captionLabels
                    onActivated: root.applyCaptions()
                }
            }

            SectionTitle { text: qsTr("Date and Time Format") }
            SectionCaption {
                text: qsTr("Choose how dates and times are displayed in list and grid views.")
            }

            Rectangle {
                width: parent.width
                height: dateColumn.implicitHeight
                radius: Colors.radius
                color: Colors.window
                border.color: Colors.border
                border.width: 1

                Column {
                    id: dateColumn
                    width: parent.width

                    RadioButton {
                        id: dateSimple
                        width: parent.width
                        text: qsTr("Simple")
                        font.pixelSize: 13
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 10
                        bottomPadding: dateSimpleExample.implicitHeight + 10
                        onToggled: if (checked) Settings.dateTimeFormat = "simple"

                        Text {
                            textFormat: Text.PlainText
                            id: dateSimpleExample
                            x: parent.leftPadding + parent.indicator.width + parent.spacing
                            y: parent.height - height - 4
                            text: qsTr("Examples: “Today, 12:33”, “3 days ago”")
                            color: Colors.textDim
                            font.pixelSize: 11
                        }
                    }

                    RadioButton {
                        id: dateDetailed
                        width: parent.width
                        text: qsTr("Detailed")
                        font.pixelSize: 13
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 10
                        bottomPadding: dateDetailedExample.implicitHeight + 10
                        onToggled: if (checked) Settings.dateTimeFormat = "detailed"

                        Text {
                            textFormat: Text.PlainText
                            id: dateDetailedExample
                            x: parent.leftPadding + parent.indicator.width + parent.spacing
                            y: parent.height - height - 4
                            text: qsTr("Examples: “08/08/2026 12:33”, “05/08/2026 12:33”")
                            color: Colors.textDim
                            font.pixelSize: 11
                        }
                    }
                }
            }

            SectionTitle { text: qsTr("Appearance") }
            SectionCaption {
                text: qsTr("Window background translucency, like a terminal's background opacity. Text and icons stay solid.")
            }

            PrefRow {
                label: qsTr("Background Opacity")

                Text {
                    textFormat: Text.PlainText
                    text: Math.round(opacitySlider.value * 100) + "%"
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                Slider {
                    id: opacitySlider
                    from: 0.5
                    to: 1.0
                    stepSize: 0.01
                    implicitWidth: root.controlWidth
                    leftPadding: 0
                    rightPadding: 0
                    onMoved: Settings.backgroundOpacity = value
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
