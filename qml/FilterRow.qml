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
    ToggleSwitch {
        id: sw
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        checked: row.checked
        onToggled: row.toggled()
    }
}
