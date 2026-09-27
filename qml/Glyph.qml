import QtQuick

// One of the app's own drawn marks (close, search, eject, arrows…) at a
// small inline size — the replacement for Unicode symbols, whose font glyphs
// vary in weight and snap to the pixel grid unevenly.
Image {
    property string name
    property color tint: Colors.text
    property int size: 12

    width: size
    height: size
    sourceSize: Qt.size(size, size)
    // At 12px and below, marks that have a 12px-grid version use it.
    readonly property bool small: size <= 12 && ["close", "eject", "forward"].indexOf(name) >= 0

    source: Colors.tint("image://fileicon/toolbar-" + name + (small ? "-small" : ""), tint)
}
