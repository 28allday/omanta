import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// The ✕ in an information or settings dialog's top-right corner. Escape
// alone was no way out for someone on the mouse (GitHub #6).
//
// Parented to the popup itself rather than its contentItem, so it sits in
// the corner above the header whatever the dialog's padding and layout are.
ToolbarButton {
    // The Dialog. Not typed Popup: the style's Dialog does not assign to it.
    required property var dialog

    parent: dialog.contentItem ? dialog.contentItem.parent : null
    anchors.top: parent ? parent.top : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.margins: 6
    z: 10
    glyph: "toolbar-close"
    tip: qsTr("Close")
    onTriggered: dialog.close()
}
