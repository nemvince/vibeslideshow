import QtQuick
import QtQuick.Controls
import Vibes 1.0

// A filter stated as a question answered by a switch. No numeric thresholds here;
// the Advanced disclosure keeps those.
//
// The switch's `checked` is driven by a Binding element rather than a plain
// `checked:` expression: an interactive Switch writes its own `checked` when it
// is clicked, which silently drops an inline binding and leaves the switch
// showing a state the model never got. With a Binding it keeps following the
// model after every click.
Item {
    id: row
    property string label: ""
    property bool checked: false
    signal toggled(bool on)

    implicitHeight: 20

    // Whole row is a target for the click; the switch sits on top of it and
    // handles its own taps.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: row.toggled(!row.checked)
    }
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
        Binding {
            target: sw
            property: "checked"
            value: row.checked
            restoreMode: Binding.RestoreBindingOrValue
        }
        onToggled: row.toggled(sw.checked)
    }
}
