import QtQuick
import Omanta
import Omanta.Runtime

// A native file drag with a bounded preview, independent of row/cell geometry.
// The card under the pointer is painted by the dragcard image provider rather
// than grabbed from a QML item: a grab finishes on the render thread, and a
// view delegate torn down mid-grab (a press that turns into opening a folder)
// left that thread working on freed memory. The provider only runs on the GUI
// thread, and only once a drag has really begun.
Item {
    id: root

    required property bool pressed
    required property bool dragging

    property var mimeData: ({})
    property bool ready: false
    property int itemCount: 0
    property string fileName: ""
    property url previewSource: ""
    property url fallbackSource: ""
    readonly property string label: itemCount > 1 ? qsTr("%1 items").arg(itemCount) : fileName
    readonly property url previewUrl: ready
        ? "image://dragcard/card?label=" + encodeURIComponent(label)
          + "&picture=" + encodeURIComponent(previewSource)
          + "&icon=" + encodeURIComponent(fallbackSource)
          + "&bg=" + encodeURIComponent(String(Qt.alpha(Colors.chrome, 1)))
          + "&fg=" + encodeURIComponent(String(Colors.text))
          + "&border=" + encodeURIComponent(String(Colors.border))
          + "&dpr=" + Screen.devicePixelRatio
        : ""

    opacity: 0

    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
    Drag.active: dragging && ready
    Drag.hotSpot: Qt.point(28, 28)
    Drag.mimeData: root.mimeData
    // Loaded (synchronously) only when a drag starts, so a plain click paints
    // nothing.
    Drag.imageSource: dragging ? root.previewUrl : ""
    Drag.onDragFinished: reset()

    function reset() {
        ready = false;
    }

    function prepare(paths, name, source, fallback) {
        mimeData = { "text/uri-list": Platform.uriList(paths) };
        itemCount = paths.length;
        fileName = name;
        fallbackSource = fallback;
        previewSource = source;
        ready = true;
    }

    onPressedChanged: if (!pressed && !dragging) reset()
    onDraggingChanged: if (!pressed && !dragging) reset()
}
