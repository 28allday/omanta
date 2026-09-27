import QtQuick
import QtQuick.Controls

// A row in an OmMenu: the theme's hover tone, theme text colours.
MenuItem {
    id: control

    implicitHeight: 30
    leftPadding: 10
    rightPadding: 10

    background: Rectangle {
        radius: 4
        color: control.highlighted && control.enabled ? Colors.hover : "transparent"
    }

    contentItem: Text {
        textFormat: Text.PlainText
        leftPadding: control.checkable && control.indicator ? control.indicator.width + control.spacing : 0
        rightPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        text: control.text
        color: control.enabled ? Colors.text : Colors.textDim
        font.pixelSize: 13
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
