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
    Switch {
        id: sw
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        checked: row.checked
        implicitHeight: 18
        implicitWidth: 34
        indicator: Rectangle {
            implicitWidth: 32
            implicitHeight: 16
            radius: 8
            color: sw.checked ? Theme.accent : Theme.line
            Rectangle {
                width: 12; height: 12; radius: 6
                y: 2
                x: sw.checked ? parent.width - width - 2 : 2
                color: sw.checked ? "#141516" : Theme.dim
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Item {}
        onToggled: row.toggled()
    }
}
