import QtQuick
import QtQuick.Controls
import Omanta.Runtime

// Flat square button. Deliberately not a Controls Button — those carry a style
// that fights the rest of the chrome.
Item {
    id: root

    property string symbol: ""
    // Unicode glyphs vary wildly in em coverage ("▾" is a speck at any size);
    // buttons whose symbol misbehaves at 15px set their own.
    property int symbolSize: 15
    // Marks with no decent Unicode codepoint render through the icon
    // provider's flat SVG glyphs instead; set to a glyphForName key.
    property string glyph: ""
    property string tip: ""
    // Toggled-on look (e.g. the search button while the bar is open).
    property bool active: false

    // `enabled` is Item's own property — redeclaring it shadows the base and
    // stops it disabling the MouseArea for us.
    signal triggered()

    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 4
        color: root.active ? Colors.selection
             : mouse.containsMouse && root.enabled ? Colors.hover : "transparent"
    }

    Text {
        anchors.centerIn: parent
        visible: root.glyph === ""
        text: root.symbol
        color: root.active ? Colors.selectionText
             : root.enabled ? Colors.text : Colors.textDim
        opacity: root.enabled ? 1.0 : 0.4
        font.pixelSize: root.symbolSize
    }

    Image {
        anchors.centerIn: parent
        visible: root.glyph !== ""
        width: 16
        height: 16
        sourceSize: Qt.size(32, 32)
        source: root.glyph === "" ? ""
              : Colors.tint("image://fileicon/" + root.glyph,
                            root.active ? Colors.selectionText
                                        : root.enabled ? Colors.text : Colors.textDim)
        opacity: root.enabled ? 1.0 : 0.4
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (root.enabled) root.triggered()
    }

    ToolTip.visible: mouse.containsMouse && root.tip !== ""
    ToolTip.text: root.tip
    ToolTip.delay: 600
}
