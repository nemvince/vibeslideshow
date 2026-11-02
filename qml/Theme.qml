pragma Singleton
import QtQuick

// One accent, one surface ramp, no decorative gradients: the footage is the
// loudest thing on screen and the chrome steps back behind it.
QtObject {
    readonly property color bg: "#111214"
    readonly property color panel: "#181a1d"
    readonly property color panelHi: "#1f2226"
    readonly property color line: "#2a2e33"
    readonly property color ink: "#e8eaed"
    readonly property color dim: "#8b929b"
    readonly property color faint: "#5d646d"
    readonly property color accent: "#f2a73b"
    readonly property color good: "#5cc98a"
    readonly property color bad: "#e5695f"

    readonly property int gap: 14
    readonly property int pad: 18
    readonly property int radius: 10

    // Motion, in one place. Every hover and selection animation in the app reads
    // these, so nothing reacts faster than anything else: `durFast` is feedback
    // under the pointer, `dur` is something appearing or resizing under the eye.
    readonly property int durFast: 110
    readonly property int dur: 170
}
