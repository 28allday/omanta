import QtQuick
import QtQuick.Controls

// Every menu in the app. The stock style paints an opaque square panel that
// ignores the background-opacity preference; this wears the same surface as
// the dialogs (chrome tone, hairline border, rounded corners) and is drawn
// inside the window so its translucency composes like the rest.
Menu {
    id: control

    popupType: Popup.Item
    padding: 6
    delegate: OmMenuItem {}

    background: Rectangle {
        implicitWidth: 220
        color: Colors.chrome
        border.color: Colors.border
        border.width: 1
        radius: Colors.radius
    }
}
