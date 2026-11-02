import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The left rail: every clip the library found, with a thumbnail and a score bar.
// Filtered-out clips stay visible but dimmed, so a filter never silently hides
// the library — you can always see what you excluded. The filters themselves
// live in the settings panel on the right.
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

        // Measured while the library is still being scanned: a count alone says
        // nothing about whether to wait.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 3
            radius: 1.5
            visible: studio.busy
            color: Theme.line
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, studio.progress))
                height: parent.height
                radius: 1.5
                color: Theme.accent
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

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: clips.count === 0
                text: studio.busy ? studio.progressLabel : "No footage yet — add a folder under Footage…"
                color: Theme.faint
                font.pixelSize: 12
            }
        }
    }
}
