import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omanta.Runtime

// Nautilus's "Visible Columns" dialog: every column in the user's order, a
// checkbox per column, ↑/↓ to reorder. Changes apply live — the list view
// follows the Settings notify. Name is locked: always visible, always first.
//
// The row model is sampled in onAboutToShow (the checked-write-severs-bindings
// pattern) and every interaction writes Settings back in full.
Dialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 380
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 80 : 560)
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Held by a property, not the default one: that would hand it to the
    // contentItem (a ScrollView here and there) before it lifts itself out.
    readonly property Item closeButton: DialogCloseButton { dialog: root }
    title: qsTr("Visible Columns")

    // Labels only — sort keys and widths live with the list view.
    readonly property var columnLabels: ({
        name: qsTr("Name"), size: qsTr("Size"), type: qsTr("Type"),
        owner: qsTr("Owner"), group: qsTr("Group"),
        permissions: qsTr("Permissions"), modified: qsTr("Modified"),
        created: qsTr("Created"), accessed: qsTr("Accessed")
    })

    onAboutToShow: {
        rows.clear();
        const visible = Settings.listVisibleColumns;
        for (const id of Settings.listColumnOrder)
            rows.append({ columnId: id, shown: visible.indexOf(id) !== -1 });
    }

    function apply() {
        const order = [];
        const visible = [];
        for (let i = 0; i < rows.count; ++i) {
            const r = rows.get(i);
            order.push(r.columnId);
            if (r.shown)
                visible.push(r.columnId);
        }
        Settings.listColumnOrder = order;
        Settings.listVisibleColumns = visible;
    }

    ListModel { id: rows }

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth

        Column {
            width: parent.width
            spacing: 4

            Repeater {
                model: rows

                delegate: Rectangle {
                    id: row

                    required property int index
                    required property string columnId
                    required property bool shown

                    readonly property bool locked: columnId === "name"

                    width: parent.width
                    height: 40
                    radius: Colors.radius
                    color: Colors.chrome

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 4

                        CheckBox {
                            checked: row.shown
                            enabled: !row.locked
                            onToggled: {
                                rows.setProperty(row.index, "shown", checked);
                                root.apply();
                            }
                        }

                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: root.columnLabels[row.columnId] ?? row.columnId
                            color: row.locked ? Colors.textDim : Colors.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        ToolbarButton {
                            glyph: "toolbar-up"
                            tip: qsTr("Move up")
                            enabled: !row.locked && row.index > 1
                            onTriggered: {
                                rows.move(row.index, row.index - 1, 1);
                                root.apply();
                            }
                        }

                        ToolbarButton {
                            glyph: "toolbar-down"
                            tip: qsTr("Move down")
                            enabled: !row.locked && row.index >= 1
                                     && row.index < rows.count - 1
                            onTriggered: {
                                rows.move(row.index, row.index + 1, 1);
                                root.apply();
                            }
                        }
                    }
                }
            }
        }
    }
}
