import QtQuick

// The small progress pie beside an operation row — Nautilus's progress
// paintable at its 14px sidebar size: a dim ring, the accent wedge
// sweeping as the operation fills, solid once done.
Canvas {
    id: pie

    property real fraction: 0
    property bool done: false

    readonly property color accentNow: Colors.accent

    width: 14
    height: 14
    anchors.verticalCenter: parent.verticalCenter

    onFractionChanged: requestPaint()
    onDoneChanged: requestPaint()
    onAccentNowChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        const cx = width / 2;
        const cy = height / 2;
        const r = width / 2 - 1;
        ctx.lineWidth = 1.5;
        ctx.strokeStyle = Colors.textDim;
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
        ctx.stroke();
        ctx.fillStyle = Colors.accent;
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        const sweep = done ? 2 * Math.PI
                           : 2 * Math.PI * Math.max(0.03, fraction);
        ctx.arc(cx, cy, r - 2, -Math.PI / 2, -Math.PI / 2 + sweep);
        ctx.closePath();
        ctx.fill();
    }
}
