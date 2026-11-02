import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The one scrollbar in the app: a 6 px gutter with a 4 px thumb that brightens
// under the pointer. The Basic style's own bar is a light groove with arrow
// buttons, which reads as a foreign object against these surfaces.
//
// Attach it rather than instantiate it in a layout:
//   ScrollBar.vertical: ThinScrollBar {}
ScrollBar {
    id: bar
    policy: ScrollBar.AsNeeded
    width: 6
    hoverEnabled: true

    background: Item {}

    contentItem: Rectangle {
        implicitWidth: 4
        radius: 2
        color: Theme.faint
        opacity: bar.pressed ? 1.0 : (bar.hovered ? 0.75 : 0.35)
        Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
    }
}
