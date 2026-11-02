import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The settings panel: what the edit looks like (one preset dropdown), which
// footage it draws from (the filters), saved looks, and the raw parameters.
// Collapsible sections keep it short; the prose that used to fill preset cards
// is now the one-line caption under the dropdown.
Rectangle {
    id: panel
    color: Theme.panel
    property bool collapsed: false
    implicitWidth: collapsed ? 34 : 340

    Behavior on implicitWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.line }

    // Collapse handle.
    Rectangle {
        anchors { left: parent.left; top: parent.top; margins: 1 }
        width: 28; height: 28
        radius: 6
        color: handle.containsMouse ? Theme.panelHi : "transparent"
        z: 2
        Text {
            anchors.centerIn: parent
            text: panel.collapsed ? "‹" : "›"
            color: Theme.dim
            font.pixelSize: 16
        }
        MouseArea {
            id: handle
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.collapsed = !panel.collapsed
        }
    }

    Flickable {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 10; bottomMargin: 10 }
        visible: !panel.collapsed
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: body
            width: parent.width
            spacing: 6

            // ---- look ------------------------------------------------------
            PanelSection {
                title: "Look"

                ComboBox {
                    id: presetCombo
                    Layout.fillWidth: true
                    implicitHeight: 32
                    font.pixelSize: 12
                    model: studio.presets
                    textRole: "name"
                    // Follow the active look until the user picks one themselves.
                    Binding {
                        target: presetCombo
                        property: "currentIndex"
                        value: Math.max(0, studio.presets.findIndex(function (p) { return p.active }))
                        when: !presetCombo.pressed && !presetCombo.popup.visible
                        restoreMode: Binding.RestoreBindingOrValue
                    }
                    onActivated: studio.applyPreset(studio.presets[index].name)
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    font.pixelSize: 11
                    text: {
                        const i = studio.presets.findIndex(function (p) { return p.active })
                        return i >= 0 ? studio.presets[i].blurb : "Hand-tuned look"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        text: studio.lookSummary
                        color: Theme.faint
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    SmallButton { text: "Reset"; onClicked: studio.restoreDefaults() }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

            // ---- footage ---------------------------------------------------
            PanelSection {
                title: "Footage"

                FilterRow {
                    Layout.fillWidth: true
                    label: "Best clips only"
                    checked: studio.filters.scoreOn === true
                    onToggled: studio.setFilter("scoreOn", checked)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Keep the frame moving"
                    checked: studio.filters.mvOn === true
                    onToggled: studio.setFilter("mvOn", checked)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Skip dark clips"
                    checked: studio.filters.junk === true
                    onToggled: studio.setFilter("junk", checked)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Only one day"
                    checked: studio.filters.dayOn === true
                    onToggled: studio.setFilter("dayOn", checked)
                }
                ComboBox {
                    visible: studio.filters.dayOn === true
                    Layout.fillWidth: true
                    implicitHeight: 28
                    font.pixelSize: 11
                    model: studio.days
                    currentIndex: Math.max(0, studio.days.indexOf(studio.filters.day))
                    onActivated: studio.setFilter("day", studio.days[index])
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Only daytime"
                    checked: studio.filters.todOn === true
                    onToggled: studio.setFilter("todOn", checked)
                }
                RowLayout {
                    visible: studio.filters.todOn === true
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "from"; color: Theme.faint; font.pixelSize: 10 }
                    Slider {
                        id: fromS
                        Layout.fillWidth: true
                        implicitHeight: 20
                        from: 0; to: 24; stepSize: 1
                        Binding {
                            target: fromS
                            property: "value"
                            value: studio.filters.tod.length ? studio.filters.tod[0].a : 8
                            when: !fromS.pressed
                            restoreMode: Binding.RestoreBindingOrValue
                        }
                        onMoved: studio.setFilter("todFrom", value)
                    }
                    Text { text: "to"; color: Theme.faint; font.pixelSize: 10 }
                    Slider {
                        id: toS
                        Layout.fillWidth: true
                        implicitHeight: 20
                        from: 0; to: 24; stepSize: 1
                        Binding {
                            target: toS
                            property: "value"
                            value: studio.filters.tod.length ? studio.filters.tod[0].b : 19
                            when: !toS.pressed
                            restoreMode: Binding.RestoreBindingOrValue
                        }
                        onMoved: studio.setFilter("todTo", value)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: studio.poolCount + " of " + studio.libraryCount + " clips in the pool"
                    color: Theme.faint
                    font.pixelSize: 10
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

            // ---- saved looks -----------------------------------------------
            PanelSection {
                title: "Saved looks"
                expanded: false

                Flow {
                    Layout.fillWidth: true
                    visible: studio.profiles.length > 0
                    spacing: 6
                    Repeater {
                        model: studio.profiles
                        delegate: Rectangle {
                            id: chip
                            required property string modelData
                            width: chipLabel.implicitWidth + 18
                            height: 26
                            radius: 6
                            color: chipHover.hovered ? Theme.panelHi : "transparent"
                            border.color: Theme.line
                            Text {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: chip.modelData
                                color: Theme.ink
                                font.pixelSize: 11
                            }
                            HoverHandler { id: chipHover }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: studio.loadProfile(chip.modelData)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        implicitHeight: 30
                        placeholderText: "Name this look"
                        font.pixelSize: 12
                        color: Theme.ink
                        background: Rectangle {
                            color: Theme.bg
                            radius: 6
                            border.color: nameField.activeFocus ? Theme.accent : Theme.line
                        }
                        onAccepted: { studio.saveProfile(text); text = ""; }
                    }
                    SmallButton {
                        text: "Save"
                        enabled: nameField.text.length > 0
                        onClicked: { studio.saveProfile(nameField.text); nameField.text = ""; }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

            // ---- advanced --------------------------------------------------
            PanelSection {
                title: "Advanced"
                expanded: false

                Repeater {
                    model: studio.advanced
                    delegate: Column {
                        id: advRow
                        required property int index
                        required property var model
                        Layout.fillWidth: true
                        spacing: 2
                        topPadding: 3
                        bottomPadding: 3

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: advRow.model.label
                                color: Theme.dim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: advRow.model.kind === "number"
                                text: advRow.model.value
                                color: Theme.faint
                                font.pixelSize: 10
                                font.family: "monospace"
                            }
                        }
                        Slider {
                            visible: advRow.model.kind === "number"
                            Layout.fillWidth: true
                            implicitHeight: 20
                            from: advRow.model.min
                            to: advRow.model.max
                            stepSize: advRow.model.step
                            value: advRow.model.value
                            onMoved: studio.setParam(advRow.model.key, value)
                        }
                        Switch {
                            visible: advRow.model.kind === "check"
                            checked: advRow.model.value === true
                            onToggled: studio.setParam(advRow.model.key, checked)
                        }
                        TextField {
                            visible: advRow.model.kind === "text"
                            Layout.fillWidth: true
                            implicitHeight: 26
                            font.pixelSize: 10
                            text: advRow.model.value ? String(advRow.model.value) : ""
                            color: Theme.ink
                            background: Rectangle { color: Theme.bg; radius: 5; border.color: Theme.line }
                            onEditingFinished: studio.setParam(advRow.model.key, text)
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true; implicitHeight: 8 }
        }
    }

    // Collapsed state: a single vertical cue instead of an empty bar.
    Text {
        anchors.centerIn: parent
        visible: panel.collapsed
        text: "Settings"
        rotation: 90
        color: Theme.faint
        font.pixelSize: 11
        font.letterSpacing: 1
    }
}
