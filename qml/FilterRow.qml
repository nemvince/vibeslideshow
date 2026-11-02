import QtQuick
import QtQuick.Controls
import Vibes 1.0

// A filter stated as a question answered by a switch. No numeric thresholds here;
// the Advanced disclosure keeps those.
Item {
    id: row
    property string label: ""
    property bool checked: false
    signal toggled()

    implicitHeight: 20

    Text {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; right: sw.left; rightMargin: 8 }
        text: row.label
        color: Theme.dim
        font.pixelSize: 11
        elide: Text.ElideRight
    }
    // Sized to sit next to a slider knob rather than a stock switch, and the "on"
    // state uses a light knob: a black dot on amber reads as a hole.
    Switch {
        id: sw
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        checked: row.checked
        implicitHeight: 12
        implicitWidth: 22
        indicator: Rectangle {
            implicitWidth: 22
            implicitHeight: 12
            radius: 6
            color: sw.checked ? Theme.accent : Theme.panelHi
            border.color: sw.checked ? Theme.accent : Theme.line
            Rectangle {
                width: 8; height: 8; radius: 4
                y: 2
                x: sw.checked ? parent.width - width - 2 : 2
                color: sw.checked ? Theme.ink : Theme.faint
                Behavior on x { NumberAnimation { duration: 110 } }
            }
        }
        contentItem: Item {}
        onToggled: row.toggled()
    }
}
