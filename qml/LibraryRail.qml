import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The left rail: every clip the library found, with a thumbnail and a score bar.
// Filtered-out clips stay visible but dimmed, so a filter never silently hides
// the library — you can always see what you excluded.
Rectangle {
    id: rail
    color: Theme.panel

    property int hovered: -1

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

    ColumnLayout {
        anchors { fill: parent; margins: Theme.gap }
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SectionLabel { text: "Library" }
            Item { Layout.fillWidth: true }
            Text {
                text: clips.visibleCount + " / " + clips.count
                color: Theme.faint
                font.pixelSize: 11
            }
        }

        // Filters in plain language: three real questions, not seven knobs.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: filterCol.implicitHeight + 20
            color: Theme.panelHi
            radius: Theme.radius
            border.color: Theme.line

            ColumnLayout {
                id: filterCol
                anchors { fill: parent; margins: 10 }
                spacing: 8

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
                    id: todRow
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
                        // The filter is the source of truth; the handle only
                        // overrides it while the user is dragging.
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
            }
        }

        // The clip list.
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: clips
            spacing: 4
            cacheBuffer: 800
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: row
                required property int index
                required property var model

                width: list.width
                height: 54
                opacity: model.inPool ? 1 : 0.32

                Rectangle {
                    anchors.fill: parent
                    radius: 7
                    color: hover.hovered ? Theme.panelHi : "transparent"
                }

                Rectangle {
                    id: thumbFrame
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 5 }
                    width: (height * 16) / 9
                    radius: 5
                    color: Theme.bg
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: model.thumb ? "file://" + model.thumb : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }
                }

                Column {
                    anchors { left: thumbFrame.right; leftMargin: 9; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        width: parent.width
                        text: model.name
                        color: Theme.ink
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Row {
                        spacing: 6
                        ScoreBadge { score: model.score; motion: model.mv }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.day.length ? model.day : "no stamp"
                            color: Theme.faint
                            font.pixelSize: 10
                        }
                        Text {
                            visible: model.proxy
                            anchors.verticalCenter: parent.verticalCenter
                            text: "proxy"
                            color: Theme.accent
                            font.pixelSize: 10
                        }
                    }
                }

                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // Click a clip to drop it into the show at the playhead.
                    onClicked: studio.newInPointAt(model.clipId, showCtl.position)
                }
            }
        }
    }
}
