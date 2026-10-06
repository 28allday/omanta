pragma Singleton

import QtQuick
import Omanta.Runtime

// The one place that knows what letting go of a drag will do, and the hint
// that says so while the drag is still in the air.
//
// Every drop target in the app calls hover() while a drag is over it and
// leave() when it moves off; the window draws `text` under the drag image.
// The hint and the drop itself read the same rule (actionFor), so what the
// label promises is exactly what happens — a drop that would do nothing
// shows no label at all.
QtObject {
    id: root

    // The Nautilus modifier convention: Ctrl forces a copy, Shift forces a
    // move, and unmodified moves within a filesystem and copies across one.
    // Returns "move", "copy", or "" when the drop would do nothing. Modifiers
    // are queried now, not taken from the drag event, because QML drag and
    // drop events do not carry them.
    function actionFor(paths, destination) {
        if (paths.length === 0 || !destination)
            return "";
        // A folder cannot be dropped into itself.
        if (paths.indexOf(destination) >= 0)
            return "";
        // Letting go in the folder things came from is a drag abandoned, not
        // a request — whatever key is held. (Ctrl here used to make a
        // "name (copy)" beside the original; a duplicate is Ctrl+C, Ctrl+V.)
        if (paths.every(p => Platform.parentPath(p) === destination))
            return "";

        const mods = Platform.keyboardModifiers();
        if (mods & Qt.ControlModifier)
            return "copy";
        if (mods & Qt.ShiftModifier)
            return "move";
        return Platform.sameFilesystem(paths[0], destination) ? "move" : "copy";
    }

    // ---- the hint ---------------------------------------------------------

    // What the label says; empty while nothing would happen.
    property string text: ""
    // Where the pointer is, in window coordinates.
    property real x: 0
    property real y: 0
    readonly property bool active: hintItem !== null

    property Item hintItem: null
    property var hintPaths: []
    property string hintDestination: ""
    property string hintLabel: ""
    property string hintFixed: ""
    property int hintModifiers: -1

    // A transfer target: the label follows the copy/move rule for
    // `destination`, naming it `label`. Call from a DropArea's entered and
    // positionChanged handlers with their DragEvent.
    function hover(item, drag, destination, label) {
        place(item, drag);
        if (item !== hintItem || destination !== hintDestination) {
            hintPaths = Platform.locationsFromUrls(drag.urls);
            hintDestination = destination;
            hintLabel = label;
            hintFixed = "";
            hintItem = item;
            hintModifiers = -1;
        }
        refresh();
    }

    // A target whose meaning never depends on the modifier keys — Trash,
    // Starred, a new bookmark: the label is given outright.
    function hoverFixed(item, drag, label) {
        place(item, drag);
        hintItem = item;
        hintPaths = [];
        hintDestination = "";
        hintFixed = label;
        text = label;
    }

    // The drag left `item`, or was dropped on it. Another target's hover()
    // may already have taken over; only the current owner clears the hint.
    function leave(item) {
        if (hintItem === item)
            clear();
    }

    function clear() {
        hintItem = null;
        hintPaths = [];
        hintDestination = "";
        hintFixed = "";
        hintModifiers = -1;
        text = "";
    }

    function place(item, drag) {
        const p = item.mapToItem(null, drag.x, drag.y);
        x = p.x;
        y = p.y;
    }

    function refresh() {
        if (!hintItem || hintFixed !== "")
            return;
        const mods = Platform.keyboardModifiers();
        if (mods === hintModifiers)
            return;
        hintModifiers = mods;
        const action = actionFor(hintPaths, hintDestination);
        text = action === "move" ? qsTr("Move to “%1”").arg(hintLabel)
             : action === "copy" ? qsTr("Copy to “%1”").arg(hintLabel)
             : "";
    }

    // Pressing or releasing Ctrl or Shift mid-drag changes the answer without
    // the pointer moving, and no event reaches a drop target for it — so the
    // label is re-read while a target is hovered.
    property Timer modifierPoll: Timer {
        interval: 50
        repeat: true
        running: root.active && root.hintFixed === ""
        onTriggered: root.refresh()
    }
}
