import QtQuick
import Vibes 1.0

// The score as a small bar rather than a bare number: it is a ranking device,
// and 0-100 in tiny type is unreadable at a glance.
Rectangle {
    id: badge
    property int score: 0
    property int motion: 0
    implicitWidth: 46
    implicitHeight: 16
    radius: 3
    color: "#66000000"

    Rectangle {
        height: parent.height
        width: parent.width * Math.max(0, Math.min(100, badge.score)) / 100
        radius: 3
        color: badge.score >= 55 ? Theme.good : badge.score >= 30 ? Theme.accent : Theme.bad
        opacity: 0.75
        Behavior on width { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: Theme.dur } }
    }
    Text {
        anchors.centerIn: parent
        text: badge.score
        color: Theme.ink
        font.pixelSize: 10
        font.family: "monospace"
    }
}
