import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The look: presets written as prose, an Advanced disclosure for the raw knobs,
// and saved looks. Collapsible so the stage can breathe.
Rectangle {
    id: panel
    color: Theme.panel
    property bool collapsed: false
    implicitWidth: collapsed ? 34 : 328

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
        anchors { fill: parent; leftMargin: 10; rightMargin: 10; topMargin: 8; bottomMargin: 10 }
        visible: !panel.collapsed
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: body
            width: parent.width
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: "Look" }
                Item { Layout.fillWidth: true }
                Text {
                    text: studio.lookSummary
                    color: Theme.faint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.maximumWidth: 190
                }
            }

            // Preset cards. Each is a sentence, not a settings dump.
            Repeater {
                model: studio.presets
                delegate: Rectangle {
                    id: card
                    required property int index
                    required property var model
                    Layout.fillWidth: true
                    implicitHeight: cardCol.implicitHeight + 20
                    radius: Theme.radius
                    color: model.active ? "#242019" : (cardHover.hovered ? Theme.panelHi : "transparent")
                    border.color: model.active ? Theme.accent : Theme.line
                    border.width: model.active ? 1.5 : 1

                    Column {
                        id: cardCol
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                        spacing: 3
                        Text {
                            width: parent.width
                            text: model.name
                            color: model.active ? Theme.accent : Theme.ink
                            font.pixelSize: 13
                            font.weight: model.active ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            width: parent.width
                            text: model.blurb
                            color: Theme.dim
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                    HoverHandler { id: cardHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: studio.applyPreset(model.name)
                    }
                }
            }

            // Saved looks, when any exist.
            RowLayout {
                Layout.fillWidth: true
                visible: studio.profiles.length > 0
                SectionLabel { text: "Saved looks" }
            }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line; Layout.topMargin: 4 }

            // Save current look.
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

            // Advanced: the full parameter list, collapsed by default.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: advHead.height + (advOpen.checked ? advCol.implicitHeight + 12 : 0)
                color: "transparent"
                border.color: Theme.line
                radius: Theme.radius
                clip: true

                Column {
                    id: advHead
                    width: parent.width
                    // Checkable header row.
                    Item {
                        width: parent.width
                        height: 34
                        Text {
                            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                            text: "Advanced"
                            color: Theme.dim
                            font.pixelSize: 12
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: advOpen.checked ? "–" : "+"
                            color: Theme.faint
                            font.pixelSize: 14
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: advOpen.checked = !advOpen.checked
                        }
                        CheckBox { id: advOpen; visible: false; checked: false }
                    }

                    Column {
                        id: advCol
                        width: parent.width
                        visible: advOpen.checked
                        spacing: 2
                        Repeater {
                            model: studio.advanced
                            delegate: Column {
                                id: advRow
                                required property int index
                                required property var model
                                width: advCol.width
                                spacing: 2
                                topPadding: 3
                                bottomPadding: 3

                                Row {
                                    width: parent.width
                                    Text {
                                        text: advRow.model.label
                                        color: Theme.dim
                                        font.pixelSize: 10
                                        width: parent.width - 90
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
                                    width: parent.width
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
                                    width: parent.width
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
                }
            }

            Item { Layout.fillHeight: true; implicitHeight: 8 }
        }
    }

    // Collapsed state: a single vertical cue instead of an empty bar.
    Text {
        anchors.centerIn: parent
        visible: panel.collapsed
        text: "Look"
        rotation: 90
        color: Theme.faint
        font.pixelSize: 11
        font.letterSpacing: 1
    }
}
