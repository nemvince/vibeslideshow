import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The settings panel: what the edit looks like (one preset picker), which footage
// it draws from (the filters), saved looks, and the raw parameters. Collapsible
// sections keep it short; the prose that used to fill preset cards is now the
// caption under the picker.
Rectangle {
    id: panel
    color: Theme.panel
    property bool collapsed: false
    // Follows the window, like the rail does, so the stage keeps a sane share of
    // the width on a small screen and the panel does not sprawl on a big one.
    implicitWidth: collapsed ? 36 : Math.round(Math.max(280, Math.min(380, parent.width * 0.245)))

    Behavior on implicitWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.line }

    // ---- header ---------------------------------------------------------
    Item {
        id: panelHeader
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: visible ? 42 : 0
        visible: !panel.collapsed

        Text {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: "SETTINGS"
            color: Theme.dim
            font.pixelSize: 10
            font.letterSpacing: 1
        }
        Item {
            anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
            width: 30; height: 30
            Text {
                anchors.centerIn: parent
                text: "›"
                color: collapseHover.hovered ? Theme.ink : Theme.faint
                font.pixelSize: 16
            }
            HoverHandler { id: collapseHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.collapsed = true
            }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.line
        }
    }

    // ---- sections -------------------------------------------------------
    Flickable {
        id: flick
        anchors {
            top: panelHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 16
            // The bar hugs the panel edge; the content column below stops short of
            // it, so nothing is ever drawn underneath.
            rightMargin: 8
            topMargin: 4
        }
        visible: !panel.collapsed
        contentHeight: body.implicitHeight + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 6
            background: Item {}
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 2
                color: Theme.faint
                opacity: parent.pressed ? 0.9 : 0.3
            }
        }

        ColumnLayout {
            id: body
            // Leave the scrollbar its own lane: it never sits on a control.
            width: parent.width - 10
            spacing: 4

            // ---- look ---------------------------------------------------
            PanelSection {
                title: "Look"
                Layout.topMargin: 6

                LookPicker {
                    Layout.fillWidth: true
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.dim
                    font.pixelSize: 11
                    lineHeight: 1.25
                    text: {
                        for (let i = 0; i < studio.looks.length; i++)
                            if (studio.looks[i].active) return studio.looks[i].blurb
                        return "A hand-tuned look."
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: studio.lookSummary
                        color: Theme.faint
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    SmallButton {
                        text: "Reset"
                        flat: true
                        onClicked: studio.restoreDefaults()
                    }
                }

                // Saving lives with the looks it saves — one manager, not a
                // separate section that only knows half the story.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        implicitHeight: 28
                        placeholderText: "Name this look"
                        font.pixelSize: 11
                        color: Theme.ink
                        background: Rectangle {
                            color: Theme.bg
                            radius: 6
                            border.color: nameField.activeFocus ? Theme.accent : Theme.line
                        }
                        onAccepted: { studio.saveProfile(text); text = ""; }
                    }
                    SmallButton {
                        text: "Save look"
                        enabled: nameField.text.length > 0
                        onClicked: { studio.saveProfile(nameField.text); nameField.text = ""; }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; Layout.topMargin: 8; Layout.bottomMargin: 4 }

            // ---- footage ------------------------------------------------
            PanelSection {
                title: "Footage"

                FilterRow {
                    Layout.fillWidth: true
                    label: "Best clips only"
                    checked: studio.filters.scoreOn === true
                    onToggled: (on) => studio.setFilter("scoreOn", on)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Keep the frame moving"
                    checked: studio.filters.mvOn === true
                    onToggled: (on) => studio.setFilter("mvOn", on)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Skip dark clips"
                    checked: studio.filters.junk === true
                    onToggled: (on) => studio.setFilter("junk", on)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Only one day"
                    checked: studio.filters.dayOn === true
                    onToggled: (on) => studio.setFilter("dayOn", on)
                }
                DayPicker {
                    Layout.fillWidth: true
                    visible: studio.filters.dayOn === true
                    onChosen: (day) => studio.setFilter("day", day)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Only daytime"
                    checked: studio.filters.todOn === true
                    onToggled: (on) => studio.setFilter("todOn", on)
                }
                FilterRow {
                    Layout.fillWidth: true
                    label: "Skip some dates"
                    checked: studio.filters.skipWindowsOn === true
                    onToggled: (on) => studio.setFilter("skipWindowsOn", on)
                }
                TextField {
                    id: skipField
                    visible: studio.filters.skipWindowsOn === true
                    Layout.fillWidth: true
                    implicitHeight: 28
                    font.pixelSize: 11
                    placeholderText: "2026-08-14±1, 2026-09-01"
                    text: studio.filters.skipWindows
                    color: Theme.ink
                    background: Rectangle {
                        color: Theme.bg
                        radius: 6
                        border.color: skipField.activeFocus ? Theme.accent : Theme.line
                    }
                    onEditingFinished: studio.setFilter("skipWindows", text)
                }
                RowLayout {
                    visible: studio.filters.todOn === true
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    spacing: 8
                    Text { text: "from"; color: Theme.faint; font.pixelSize: 10 }
                    DarkSlider {
                        id: fromS
                        Layout.fillWidth: true
                        from: 0; to: 24; stepSize: 1
                        fillColor: Theme.dim
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
                    DarkSlider {
                        id: toS
                        Layout.fillWidth: true
                        from: 0; to: 24; stepSize: 1
                        fillColor: Theme.dim
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
                    Layout.topMargin: 2
                    text: studio.poolCount + " of " + studio.libraryCount + " clips in the pool"
                    color: Theme.faint
                    font.pixelSize: 11
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line; Layout.topMargin: 8; Layout.bottomMargin: 4 }

            // ---- advanced ------------------------------------------------
            PanelSection {
                title: "Advanced"
                expanded: false

                Repeater {
                    model: studio.advanced
                    delegate: ColumnLayout {
                        id: advRow
                        required property int index
                        required property var model
                        Layout.fillWidth: true
                        spacing: 2

                        // switch row
                        RowLayout {
                            visible: advRow.model.kind === "check"
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: advRow.model.label
                                color: Theme.dim
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            ToggleSwitch {
                                id: advSw
                                Layout.alignment: Qt.AlignVCenter
                                // A Binding element, not `checked:`: the switch has to
                                // keep following the params after it is clicked, and a
                                // plain binding is dropped by the first interaction.
                                Binding {
                                    target: advSw
                                    property: "checked"
                                    value: studio.params[advRow.model.key] === true
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                                onToggled: studio.setParam(advRow.model.key, checked)
                            }
                        }

                        // number row: label, live value, slider
                        Text {
                            visible: advRow.model.kind === "number"
                            Layout.fillWidth: true
                            text: advRow.model.label
                            color: Theme.dim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            visible: advRow.model.kind === "number"
                            Layout.fillWidth: true
                            spacing: 6
                            DarkSlider {
                                id: advSlider
                                Layout.fillWidth: true
                                implicitHeight: 22
                                from: advRow.model.min
                                to: advRow.model.max
                                stepSize: advRow.model.step
                                fillColor: Theme.dim
                                // Same rule as every other slider: the model drives the
                                // handle except while it is being dragged, so a rebuild
                                // mid-drag cannot snap it back.
                                Binding {
                                    target: advSlider
                                    property: "value"
                                    value: {
                                        const v = studio.params[advRow.model.key]
                                        return v === undefined ? advRow.model.value : v
                                    }
                                    when: !advSlider.pressed
                                    restoreMode: Binding.RestoreBindingOrValue
                                }
                                onMoved: studio.setParam(advRow.model.key, value)
                            }
                            Text {
                                Layout.preferredWidth: 44
                                horizontalAlignment: Text.AlignRight
                                text: {
                                    const v = studio.params[advRow.model.key]
                                    return v === undefined ? "" : String(v)
                                }
                                color: Theme.faint
                                font.pixelSize: 10
                                font.family: "monospace"
                            }
                        }

                        // text row
                        TextField {
                            visible: advRow.model.kind === "text"
                            Layout.fillWidth: true
                            implicitHeight: 26
                            font.pixelSize: 10
                            text: {
                                const v = studio.params[advRow.model.key]
                                return v === undefined ? "" : String(v)
                            }
                            color: Theme.ink
                            background: Rectangle { color: Theme.bg; radius: 5; border.color: Theme.line }
                            onEditingFinished: studio.setParam(advRow.model.key, text)
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true; implicitHeight: 12 }
        }
    }

    // ---- collapsed ------------------------------------------------
    Item {
        anchors.fill: parent
        visible: panel.collapsed

        Text {
            anchors.centerIn: parent
            text: "SETTINGS"
            rotation: 90
            color: Theme.faint
            font.pixelSize: 10
            font.letterSpacing: 1
        }
        Item {
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 34
            Text {
                anchors.centerIn: parent
                text: "‹"
                color: expandHover.hovered ? Theme.ink : Theme.faint
                font.pixelSize: 16
            }
            HoverHandler { id: expandHover }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.collapsed = false
            }
        }
    }
}
