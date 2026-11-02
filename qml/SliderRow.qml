import QtQuick
import QtQuick.Layouts
import Vibes 1.0

// A slider that says what it means: a plain-language label, the value as a human
// would say it ("≈ 2.4 s", "quick dissolve"), and the track underneath. The raw
// number is never the point of the control.
ColumnLayout {
    id: row
    Layout.fillWidth: true
    spacing: 2

    property string label: ""
    property string valueText: ""
    property string hint: ""
    property real from: 0
    property real to: 1
    property real step: 0.01
    property real value: 0
    signal moved(real value)

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
        Text {
            text: row.valueText
            color: Theme.dim
            font.pixelSize: 10
        }
    }

    DarkSlider {
        id: slider
        Layout.fillWidth: true
        implicitHeight: 18
        from: row.from
        to: row.to
        stepSize: row.step
        // The model drives the handle except while it is being dragged, so a
        // preset applied mid-drag cannot snap it back under the finger.
        Binding {
            target: slider
            property: "value"
            value: row.value
            when: !slider.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
        onMoved: row.moved(value)
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
