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

    implicitHeight: 22

    Text {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; right: sw.left; rightMargin: 8 }
        text: row.label
        color: Theme.dim
        font.pixelSize: 11
        elide: Text.ElideRight
    }
    // Smaller than a stock switch: this is a quiet filter list, not a settings
    // page. Track and knob stay low-contrast until switched on.
    Switch {
        id: sw
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        checked: row.checked
        implicitHeight: 16
        implicitWidth: 28
        indicator: Rectangle {
            implicitWidth: 28
            implicitHeight: 14
            radius: 7
            color: sw.checked ? Theme.accent : Theme.panelHi
            border.color: sw.checked ? Theme.accent : Theme.line
            Rectangle {
                width: 10; height: 10; radius: 5
                y: 2
                x: sw.checked ? parent.width - width - 2 : 2
                color: sw.checked ? "#141516" : Theme.faint
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Item {}
        onToggled: row.toggled()
    }
}
