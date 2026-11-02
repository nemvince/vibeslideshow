import QtQuick
import QtQuick.Controls
import Vibes 1.0

// The edit, as a timeline: a time ruler, segments proportional to their length, a
// playhead over them, and the whole surface is the seek control.
//
// Zoom stretches the song past the width of the window: at 1× the whole edit fits
// exactly (the overview), and further in the strip scrolls, so segments get wide
// enough to carry a preview of the clip they contain. The playhead is followed
// while the show plays; nothing else moves the view under the user's hand.
Item {
    id: tl
    implicitHeight: 108
    // Scrubbing and segment picking are off while the library is being rebuilt or
    // the song is still being read: the edit on screen is about to be replaced.
    enabled: studio.workspaceEnabled

    // 1× = the whole song fits the window. Beyond that the strip scrolls.
    property real zoom: 1.0
    readonly property real maxZoom: 24.0
    readonly property bool zoomed: zoom > 1.001
    // How much of the song is on screen, in seconds: what the ruler ticks against.
    readonly property real visibleSeconds: duration / Math.max(1, zoom)

    // Prefer the player's own duration, but never depend on it: while mpv is still
    // opening the file it reports 0, and every segment would be screen-wide.
    readonly property real duration: {
        if (showCtl.duration > 0) return showCtl.duration
        if (timeline.songDuration > 0) return timeline.songDuration
        return 1
    }
    readonly property real progressFrac: Math.max(0, Math.min(1, showCtl.position / duration))
    // Before a song is loaded `duration` is the 1-second fallback, and every ruler
    // label would read 0:00. Say nothing rather than something wrong.
    readonly property bool knownDuration: showCtl.duration > 0 || timeline.songDuration > 0
    readonly property int rulerHeight: 18

    readonly property real contentWidth: Math.max(width, Math.round(width * zoom))
    readonly property real maxScroll: Math.max(0, contentWidth - width)

    function timeAt(x) { return Math.max(0, Math.min(1, x / Math.max(1, contentWidth))) * duration }
    function xAt(t) { return duration > 0 ? (t / duration) * contentWidth : 0 }
    function seekTo(x) { studio.seek(timeAt(x)) }

    function setZoom(z) {
        const next = Math.max(1, Math.min(maxZoom, z))
        if (Math.abs(next - zoom) < 0.001) return
        // Anchor on the seek head, not on the middle of the window: zooming in
        // walks into the music around the playhead, which is the part you are
        // working on. Where the head sits in the view is where it stays; if it is
        // off screen (you scrolled away), it is brought back to the middle.
        const headX = progressFrac * contentWidth;
        const offset = headX - flick.contentX;
        const onScreen = offset >= 0 && offset <= width;
        const frac = onScreen ? Math.max(0.04, Math.min(0.96, offset / Math.max(1, width))) : 0.5;
        zoom = next
        flick.contentX = Math.max(0, Math.min(maxScroll, progressFrac * contentWidth - frac * width))
    }
    function zoomIn() { setZoom(zoom * 1.6) }
    function zoomOut() { setZoom(zoom / 1.6) }

    // Keep the playhead on screen while the show runs. Called off position changes,
    // so it does nothing at 1× and costs one comparison otherwise.
    function followPlayhead() {
        if (!zoomed || width <= 0) return
        const px = progressFrac * contentWidth
        const left = flick.contentX + width * 0.12
        const right = flick.contentX + width * 0.88
        if (px < left || px > right)
            flick.contentX = Math.max(0, Math.min(maxScroll, px - width / 2))
    }

    // ---- ruler ticks ------------------------------------------------------
    // A "nice" interval that fits the visible span, so zooming in shows seconds
    // and zooming out shows minutes instead of seven arbitrary fractions.
    readonly property real tickStep: {
        const raw = visibleSeconds / 6
        const steps = [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600]
        for (let i = 0; i < steps.length; i++) if (steps[i] >= raw) return steps[i]
        return 900
    }
    function tickTimes() {
        if (!knownDuration) return []
        const first = Math.ceil(timeAt(flick.contentX) / tickStep) * tickStep
        const last = timeAt(flick.contentX + width)
        const out = []
        for (let t = first; t <= last + 1e-6 && out.length < 32; t += tickStep)
            out.push(Math.round(t * 1000) / 1000)
        return out
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        radius: Theme.radius
        border.color: Theme.line
        clip: true

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: tl.contentWidth
            contentHeight: height
            // Panning is driven from the strip's own mouse area (left-drag scrubs,
            // shift/right-drag pans, middle-drag pans), not by the flickable: a
            // draggable flickable would steal the scrub drag on the way.
            interactive: false
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            // Only there when there is something to scroll, and draggable: the
            // strip answers the wheel, a drag on it and this bar.
            ScrollBar.horizontal: ThinScrollBar {
                height: 8
                policy: tl.zoomed ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Item {
                id: canvas
                width: flick.contentWidth
                height: flick.height

                // ---- time ruler ----
                Item {
                    id: ruler
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: tl.rulerHeight

                    Repeater {
                        model: tl.tickTimes()
                        delegate: Item {
                            required property real modelData
                            x: Math.round(tl.xAt(modelData))
                            width: 1
                            height: ruler.height

                            Rectangle { width: 1; height: 5; y: ruler.height - height; color: Theme.line }
                            Text {
                                x: 4
                                text: studio.formatTime(modelData)
                                color: Theme.faint
                                font.pixelSize: 9
                                font.family: "monospace"
                            }
                        }
                    }

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 1
                        color: Theme.line
                    }
                }

                // ---- segments ----
                Row {
                    id: track
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: ruler.bottom
                        bottom: parent.bottom
                    }
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    anchors.bottomMargin: 1
                    spacing: 0

                    Repeater {
                        model: timeline
                        delegate: Item {
                            id: cell
                            required property int index
                            required property var model
                            height: track.height
                            width: Math.max(1, Math.min(track.width, model.segLen / tl.duration * track.width))

                            // A preview needs room to be a picture rather than a
                            // smudge: the wider the segment, the more it can show.
                            readonly property bool showThumb: width >= 28
                            readonly property bool showName: width >= 120

                            Rectangle {
                                anchors.fill: parent
                                color: cell.model.segSelected ? "#2c3138"
                                       : (cellHover.hovered ? "#262a2f" : Theme.panelHi)
                                clip: true
                                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                                Image {
                                    anchors.fill: parent
                                    visible: cell.showThumb
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: true
                                    source: cell.model.segThumb && cell.showThumb ? "file://" + cell.model.segThumb : ""
                                    opacity: 0.8
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    color: cell.model.segColor
                                    opacity: cell.model.segThumb && cell.showThumb ? 0.15 : 0.5
                                }
                                // Name and length once there is room for them; with
                                // no preview the label is the only identification.
                                Text {
                                    visible: cell.showName
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        bottom: parent.bottom
                                        leftMargin: 5
                                        rightMargin: 5
                                        bottomMargin: 3
                                    }
                                    text: cell.model.segName
                                    color: Theme.ink
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                    style: Text.Outline
                                    styleColor: "#99000000"
                                }
                                // A hairline between cuts: a dense edit still reads as
                                // separate shots instead of a smear.
                                Rectangle {
                                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                                    width: 1
                                    color: Theme.bg
                                }
                            }

                            HoverHandler { id: cellHover }

                            // Selection: top and bottom rules, so a thin segment cannot
                            // lose its highlight. Faded in rather than popped, because
                            // the eye is on the frame, not on this.
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; top: parent.top }
                                height: 2
                                color: Theme.accent
                                visible: opacity > 0
                                opacity: cell.model.segSelected ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 2
                                color: Theme.accent
                                visible: opacity > 0
                                opacity: cell.model.segSelected ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
                            }
                        }
                    }
                }

                // ---- playhead ----
                Item {
                    x: Math.max(0, Math.min(canvas.width - width, tl.progressFrac * canvas.width - width / 2))
                    width: 11
                    height: canvas.height
                    visible: opacity > 0
                    opacity: timeline.count > 0 ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.dur } }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 2
                        height: parent.height
                        color: Theme.accent
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 9
                        height: 7
                        radius: 2
                        color: Theme.accent
                    }
                }

                // ---- scrubbing, panning and the clip menu ----
                // Left-drag scrubs (the strip is the seek control, and always was);
                // shift-drag or middle-drag pans the strip; a click still places the
                // playhead and selects the segment under it. Right-click is the
                // clip menu's button now, so right-drag no longer pans.
                MouseArea {
                    id: surface
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    property bool panning: false
                    property real lastX: 0
                    cursorShape: panning ? Qt.ClosedHandCursor : Qt.PointingHandCursor

                    function pan(dx) {
                        flick.contentX = Math.max(0, Math.min(tl.maxScroll, flick.contentX - dx))
                    }

                    onPressed: (m) => {
                        if (m.button === Qt.RightButton) {
                            const idx = timeline.indexAt(tl.timeAt(m.x))
                            if (idx >= 0) {
                                timeline.selected = idx
                                tl.popupClipMenu(m.x, m.y)
                            }
                            return
                        }
                        panning = m.button === Qt.MiddleButton || (m.modifiers & Qt.ShiftModifier)
                        lastX = m.x
                        if (!panning) tl.seekTo(m.x)
                    }
                    onPositionChanged: (m) => {
                        if (!pressed || m.button === Qt.RightButton) return
                        if (panning) {
                            pan(m.x - lastX)
                            lastX = m.x
                        } else {
                            tl.seekTo(m.x)
                        }
                    }
                    onReleased: (m) => {
                        if (m.button === Qt.LeftButton && !panning) {
                            const idx = timeline.indexAt(tl.timeAt(m.x))
                            if (idx >= 0) timeline.selected = idx
                        }
                        panning = false
                    }
                    onCanceled: panning = false
                }

                // Wheel scrolls (either axis: a trackpad swipes sideways), and
                // ctrl+wheel zooms around the middle of the view.
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    blocking: true
                    onWheel: (event) => {
                        // Trackpads and precision wheels report pixel deltas instead
                        // of the 120-unit notch, and "natural scrolling" reports
                        // them inverted: honour both, or a touchpad scrolls the
                        // strip the wrong way and stops dead at the left edge.
                        const sign = event.inverted ? -1 : 1
                        const dx = sign * (event.angleDelta.x || event.pixelDelta.x)
                        const dy = sign * (event.angleDelta.y || event.pixelDelta.y)

                        if (event.modifiers & Qt.ControlModifier) {
                            // Proportional to the gesture: one notch is about 23 %,
                            // one touchpad tick a percent or two, so a smooth
                            // scroll does not slam the zoom between the limits.
                            const step = Math.max(-240, Math.min(240, dy))
                            if (step !== 0) tl.setZoom(tl.zoom * Math.pow(2, step / 400))
                        } else if (tl.zoomed) {
                            // A sideways swipe is already the direction we scroll
                            // in; a vertical wheel scrolls the song left and right.
                            if (Math.abs(dx) > Math.abs(dy)) surface.pan(-dx)
                            else surface.pan(dy)
                        }
                        event.accepted = true
                    }
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: timeline.count === 0 && tl.enabled
            text: "Pick a song and a look, then press Generate"
            color: Theme.faint
            font.pixelSize: 12
        }

        // The same dimming the rest of the workspace gets while a pass runs, so a
        // disabled strip cannot be mistaken for a broken one.
        Rectangle {
            id: lockVeil
            anchors.fill: parent
            visible: !tl.enabled
            color: Theme.bg
            opacity: 0.55
        }
    }

    // The clip menu: right-click acts on the shot under the pointer, so the
    // selection follows the click before the menu opens.
    ClipMenu {
        id: clipMenu
        onPicked: (id) => timeline.runCommand(id)
    }

    // Open the menu at the pointer, kept inside the strip: a right-click near the
    // bottom of the timeline should not push the menu off the window.
    function popupClipMenu(x, y) {
        const h = clipMenu.implicitHeight
        clipMenu.x = Math.max(0, Math.min(tl.width - clipMenu.width, x))
        clipMenu.y = (y + h > tl.height) ? Math.max(0, y - h) : y
        clipMenu.open()
    }

    Connections {
        target: showCtl
        function onPositionChanged() { tl.followPlayhead() }
    }
}
