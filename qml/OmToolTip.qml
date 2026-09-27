import QtQuick
import QtQuick.Controls

// Every tooltip in the app, on the same surface as OmMenu and the dialogs
// rather than the stock style's light-bordered box.
ToolTip {
    id: control

    delay: 600
    popupType: Popup.Item
    topPadding: 5
    bottomPadding: 5
    leftPadding: 8
    rightPadding: 8

    contentItem: Text {
        textFormat: Text.PlainText
        text: control.text
        color: Colors.text
        font.pixelSize: 12
    }

    background: Rectangle {
        color: Colors.chrome
        border.color: Colors.border
        border.width: 1
        radius: Colors.radius
    }
}
