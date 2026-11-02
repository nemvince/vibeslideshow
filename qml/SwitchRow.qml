import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// A setting the user can turn on and off, with the sentence that explains what
// turning it on does. Used for everything that is a yes/no about the video.
ColumnLayout {
    id: row
    Layout.fillWidth: true
    spacing: 2

    property string label: ""
    property string hint: ""
    property bool checked: false
    signal toggled(bool on)

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: row.label
            color: Theme.ink
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        ToggleSwitch {
            id: sw
            Layout.alignment: Qt.AlignVCenter
            // A Binding, not `checked:`: an interactive switch drops an inline
            // binding the first time it is clicked and stops following the model.
            Binding {
                target: sw
                property: "checked"
                value: row.checked
                restoreMode: Binding.RestoreBindingOrValue
            }
            onToggled: row.toggled(sw.checked)
        }
    }

    Text {
        Layout.fillWidth: true
        visible: row.hint.length > 0
        text: row.hint
        color: Theme.faint
        font.pixelSize: 10
        lineHeight: 1.2
        wrapMode: Text.WordWrap
    }
}
