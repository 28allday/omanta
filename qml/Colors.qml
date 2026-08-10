pragma Singleton

import QtQuick
import Omanta.Runtime

// One place for every colour. When the active Omarchy theme's colors.toml is
// present, all roles come from it — omanta then matches the terminal, the
// bar and every other themed surface, and follows `omarchy theme set` live.
// Without it (non-Omarchy system), the built-in palette below tracks the
// portal's light/dark setting instead.
QtObject {
    readonly property bool dark: Theme.darkMode
    readonly property bool themed: Theme.hasThemeColors

    readonly property color window: themed ? Theme.windowColor : (dark ? "#101010" : "#fbfbfb")
    readonly property color chrome: themed ? Theme.chromeColor : (dark ? "#161616" : "#f0f0f0")
    readonly property color border: themed ? Theme.borderColor : (dark ? "#2a2a2a" : "#dcdcdc")

    readonly property color text: themed ? Theme.textColor : (dark ? "#eeeeee" : "#1c1c1c")
    readonly property color textDim: themed ? Theme.textDimColor : (dark ? "#8a8a8a" : "#6b6b6b")

    readonly property color accent: themed ? Theme.accentColor : (dark ? "#7aa2f7" : "#3457d5")
    readonly property color selection: themed ? Theme.selectionColor : (dark ? "#2c3a5a" : "#d3e0ff")
    // What a label sitting on the selection colour must be drawn in — the
    // built-in selections are mid-tones the normal text reads fine on.
    readonly property color selectionText: themed ? Theme.selectionTextColor : (dark ? "#eeeeee" : "#1c1c1c")
    readonly property color hover: themed ? Theme.hoverColor : (dark ? "#1e1e1e" : "#eaeaea")

    readonly property color error: themed ? Theme.errorColor : "#f7768e"

    readonly property int radius: 6
    readonly property int rowHeight: 30

    // Ask the icon provider for the flat theme-coloured glyph instead of the
    // GTK theme icon. The colour rides the URL (minus its '#', which a URL
    // would read as a fragment), so theme switches and selection changes
    // re-render through ordinary bindings.
    function tint(iconSource, color) {
        return iconSource + "?c=" + String(color).substring(1);
    }
}
