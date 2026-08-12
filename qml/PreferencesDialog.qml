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
    padding: 20
    topPadding: 12

    background: Rectangle {
        color: Colors.chrome
        border.color: Colors.border
        border.width: 1
        radius: Colors.radius
    }

    header: Label {
        text: root.title
        color: Colors.text
        font.pixelSize: 16
        font.bold: true
        leftPadding: 20
        topPadding: 18
    }

    onAboutToShow: {
        syncFromSettings();
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
            anchors.rightMargin: 10
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: label
                color: Colors.text
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }
    }

    component SectionTitle: Text {
        width: parent.width
        color: Colors.text
        font.pixelSize: 14
        font.bold: true
        topPadding: 16
    }

    component SectionCaption: Text {
        width: parent.width
        color: Colors.textDim
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        bottomPadding: 4
    }

    contentItem: ScrollView {
        id: scroller

        clip: true
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 8

            SectionTitle { text: qsTr("General"); topPadding: 0 }

            PrefRow {
                label: qsTr("Sort Folders Before Files")
                Switch {
                    id: foldersFirstSwitch
                    onToggled: Settings.sortFoldersFirst = checked
                }
            }

            PrefRow {
                label: qsTr("Action to Open Items")
                ComboBox {
                    id: clickCombo
                    model: [qsTr("Double-Click"), qsTr("Single-Click")]
                    implicitWidth: 170
                    onActivated: Settings.clickPolicy = currentIndex === 1 ? "single" : "double"
                }
            }

            PrefRow {
                label: qsTr("Expandable Folders in List View")
                Switch {
                    id: treeViewSwitch
                    onToggled: Settings.useTreeView = checked
                }
            }

            SectionTitle { text: qsTr("Optional Context Menu Actions") }
            SectionCaption {
                text: qsTr("Show more actions in the menus. Keyboard shortcuts can be used even if the actions are not shown.")
            }

            PrefRow {
                label: qsTr("Create Link")
                Switch {
                    id: createLinkSwitch
                    onToggled: Settings.showCreateLink = checked
                }
            }

            PrefRow {
                label: qsTr("Delete Permanently")
                Switch {
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
                ComboBox {
                    id: searchCombo
                    model: root.policyLabels
                    implicitWidth: 190
                    onActivated: Settings.searchInSubfolders = root.policyValues[currentIndex]
                }
            }

            PrefRow {
                label: qsTr("Show Thumbnails")
                ComboBox {
                    id: thumbnailsCombo
                    model: root.policyLabels
                    implicitWidth: 190
                    onActivated: Settings.showThumbnails = root.policyValues[currentIndex]
                }
            }

            PrefRow {
                label: qsTr("Count Number of Files in Folders")
                ComboBox {
                    id: itemCountsCombo
                    model: root.policyLabels
                    implicitWidth: 190
                    onActivated: Settings.showDirectoryItemCounts = root.policyValues[currentIndex]
                }
            }

            SectionTitle { text: qsTr("Icon View Captions") }
            SectionCaption {
                text: qsTr("Add information to be displayed beneath file and folder names. More information will appear when zooming closer.")
            }

            PrefRow {
                label: qsTr("First")
                ComboBox {
                    id: captionFirst
                    model: root.captionLabels
                    implicitWidth: 170
                    onActivated: root.applyCaptions()
                }
            }

            PrefRow {
                label: qsTr("Second")
                ComboBox {
                    id: captionSecond
                    model: root.captionLabels
                    implicitWidth: 170
                    onActivated: root.applyCaptions()
                }
            }

            PrefRow {
                label: qsTr("Third")
                ComboBox {
                    id: captionThird
                    model: root.captionLabels
                    implicitWidth: 170
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
                        bottomPadding: dateSimpleExample.implicitHeight + 10
                        onToggled: if (checked) Settings.dateTimeFormat = "simple"

                        Text {
                            id: dateSimpleExample
                            x: parent.indicator.width + parent.spacing + 6
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
                        bottomPadding: dateDetailedExample.implicitHeight + 10
                        onToggled: if (checked) Settings.dateTimeFormat = "detailed"

                        Text {
                            id: dateDetailedExample
                            x: parent.indicator.width + parent.spacing + 6
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
                    text: Math.round(opacitySlider.value * 100) + "%"
                    color: Colors.textDim
                    font.pixelSize: 12
                }

                Slider {
                    id: opacitySlider
                    from: 0.5
                    to: 1.0
                    stepSize: 0.01
                    implicitWidth: 170
                    onMoved: Settings.backgroundOpacity = value
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
