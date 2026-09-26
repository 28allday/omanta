import QtQuick
import Omanta
import Omanta.Runtime

// A native file drag with a bounded preview, independent of row/cell geometry.
// The transparent parent keeps the card out of the view; grabToImage on the
// card renders its own contents without inheriting the parent's opacity.
Item {
    id: root

    required property bool pressed
    required property bool dragging

    property var mimeData: ({})
    property var grabResult: null
    property bool ready: false
    property bool capturePending: false
    property int generation: 0
    property int itemCount: 0
    property string fileName: ""
    property url previewSource: ""
    property url fallbackSource: ""
    readonly property url previewUrl: grabResult ? grabResult.url : ""

    opacity: 0

    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
    Drag.active: dragging && ready
    Drag.hotSpot: Qt.point(28, 28)
    Drag.mimeData: root.mimeData
    Drag.imageSource: root.previewUrl
    Drag.onDragFinished: reset()

    function reset() {
        ++generation;
        capturePending = false;
        captureTimeout.stop();
        ready = false;
        grabResult = null;
    }

    function prepare(paths, name, source, fallback) {
        reset();
        mimeData = { "text/uri-list": Platform.uriList(paths) };
        itemCount = paths.length;
        fileName = name;
        fallbackSource = fallback;
        previewSource = source;
        capturePending = true;
        // A ready thumbnail is preferred, but a slow remote thumbnail must
        // never hold up dragging: after a short grace period use its icon.
        captureTimeout.restart();
        captureCheck.restart();
    }

    function captureIfReady() {
        if (preview.status !== Image.Loading)
            capture();
    }

    function capture() {
        if (!capturePending || !pressed)
            return;
        capturePending = false;
        captureTimeout.stop();
        const request = generation;
        const started = card.grabToImage(result => {
            if (request !== generation || !pressed)
                return;
            // Retain the grab for the entire drag: its URL is backed by it.
            grabResult = result;
            ready = true;
        });
        // Even if a window cannot be captured, the file payload can be dragged.
        if (!started)
            ready = true;
    }

    // Destruction clears the context before the item itself is deleted;
    // neither timer may fire into that gap.
    Component.onDestruction: {
        captureCheck.stop();
        captureTimeout.stop();
    }

    onPressedChanged: if (!pressed && !dragging) reset()
    onDraggingChanged: if (!pressed && !dragging) reset()

    // Deferred to the next event-loop pass, like Qt.callLater, but owned by
    // this item: a view left right after a press tears the delegate down,
    // and a queued Qt.callLater would still run against its dead context.
    Timer {
        id: captureCheck
        interval: 0
        onTriggered: root.captureIfReady()
    }

    Timer {
        id: captureTimeout
        interval: 80
        onTriggered: root.capture()
    }

    Rectangle {
        id: card

        width: Math.min(300, Math.max(120, label.implicitWidth + 70))
        height: 56
        radius: Colors.radius
        color: Qt.alpha(Colors.chrome, 1)
        border.color: Colors.border

        Image {
            id: fallback

            x: 10
            y: 10
            width: 36
            height: 36
            source: root.fallbackSource
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            visible: preview.status !== Image.Ready
        }

        Image {
            id: preview

            anchors.fill: fallback
            source: root.previewSource
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: status === Image.Ready
            onStatusChanged: captureCheck.restart()
        }

        Text {
            id: label

            x: 56
            width: parent.width - x - 12
            height: parent.height
            text: root.itemCount > 1 ? qsTr("%1 items").arg(root.itemCount) : root.fileName
            textFormat: Text.PlainText
            font.pixelSize: 13
            color: Colors.text
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
        }
    }
}
