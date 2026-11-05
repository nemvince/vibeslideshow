import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vibes 1.0

// The settings panel: six tools, each named after what it does to the video, each
// with a sentence saying what moving it changes. Nothing here is a bare knob —
// a control shows its effect in words ("≈ 2.4 s", "quick dissolve"), and the
// numbers behind it stay in the model.
Rectangle {
    id: panel
    color: Theme.panel
    property bool collapsed: false
    implicitWidth: collapsed ? 36 : Math.round(Math.max(280, Math.min(380, parent.width * 0.245)))

    Behavior on implicitWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.line }

    // ---- plain language helpers ------------------------------------------
    function paceWord(v) {
        if (v <= 0.3) return "calm"
        if (v < 0.95) return "even"
        if (v < 1.5) return "driving"
        return "frenetic"
    }
    function seconds(v) {
        return (Math.round(v * 10) / 10).toFixed(1) + " s"
    }
    // Beats turned into time, so "longest hold" reads as something you can see on
    // screen instead of a beat count.
    function holdText(beats) {
        const bpm = studio.bpm
        if (!(bpm > 0)) return beats + (beats === 1 ? " beat" : " beats")
        return "up to " + seconds(beats * 60 / bpm)
    }
    function fadeText(ms) {
        if (ms <= 0) return "hard cut"
        if (ms <= 120) return "quick dissolve · " + Math.round(ms) + " ms"
        return "soft dissolve · " + Math.round(ms) + " ms"
    }
    function hourText(h) {
        const hh = Math.floor(h) % 24
        const mm = Math.round((h - Math.floor(h)) * 60)
        return (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
    }
    function energyWord(v) {
        if (v < 0.25) return "still"
        if (v < 0.5) return "unhurried"
        if (v < 0.75) return "moving"
        return "full-on"
    }
    function danceWord(v) {
        if (v < 0.25) return "loose"
        if (v < 0.55) return "steady"
        return "on the grid"
    }
    function moodWord(v) {
        if (v < 0.35) return "dark"
        if (v < 0.65) return "warm"
        return "bright"
    }
    function shapeValue() {
        if (studio.filters.arOn !== true) return "any"
        const ar = studio.filters.ar
        if (Math.abs(ar - 16 / 9) < 0.03) return "16:9"
        if (Math.abs(ar - 9 / 16) < 0.03) return "9:16"
        if (Math.abs(ar - 4 / 3) < 0.03) return "4:3"
        if (Math.abs(ar - 1) < 0.03) return "1:1"
        return "any"
    }
    function shapeRatio(v) {
        if (v === "9:16") return 9 / 16
        if (v === "4:3") return 4 / 3
        if (v === "1:1") return 1
        return 16 / 9
    }

    // ---- header ---------------------------------------------------------
    Item {
        id: panelHeader
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: visible ? 42 : 0
        visible: !panel.collapsed

        SectionLabel {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: "Tools"
            color: Theme.dim
            font.pixelSize: 10
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

    // ---- tools ----------------------------------------------------------
    Flickable {
        id: flick
        objectName: "toolsScroll"
        anchors {
            top: panelHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            leftMargin: 12
            rightMargin: 8
            topMargin: 10
        }
        visible: !panel.collapsed
        contentHeight: tools.implicitHeight + 14
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            id: tools
            width: parent.width - 10
            spacing: 8

            // ---- track --------------------------------------------------
            // What the analysis heard. These are measurements of the audio, and
            // the edit follows them: movement follows energy, holds follow
            // danceability, and the section map decides where shots breathe.
            ToolCard {
                title: "Track"
                hint: "Measured from the song itself — this is what the cut follows."

                Text {
                    Layout.fillWidth: true
                    text: studio.songAnalysed
                          ? Math.round(studio.bpm) + " bpm" + (studio.trackKey.length ? " · " + studio.trackKey : "")
                          : "No song yet"
                    color: Theme.ink
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                MeterRow {
                    visible: studio.songAnalysed
                    label: "Energy"
                    value: studio.energy
                    word: panel.energyWord(studio.energy)
                }
                MeterRow {
                    visible: studio.songAnalysed
                    label: "Danceability"
                    value: studio.danceability
                    word: panel.danceWord(studio.danceability)
                    fill: Theme.good
                }
                MeterRow {
                    visible: studio.songAnalysed
                    label: "Mood"
                    value: studio.valence
                    word: panel.moodWord(studio.valence)
                    fill: Theme.accent
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: studio.songAnalysed
                    Text {
                        Layout.fillWidth: true
                        text: "Level " + studio.loudnessDb.toFixed(1) + " dBFS"
                        color: Theme.faint
                        font.pixelSize: 10
                    }
                    Text {
                        text: studio.formatTime(timeline.songDuration)
                        color: Theme.faint
                        font.pixelSize: 10
                    }
                }
            }

            // ---- look ---------------------------------------------------
            ToolCard {
                title: "Look"
                hint: "The starting point. Pick one and adjust anything below it."

                LookPicker { Layout.fillWidth: true }

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
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: studio.lookSummary
                        color: Theme.faint
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    SmallButton {
                        text: "Defaults"
                        flat: true
                        onClicked: studio.restoreDefaults()
                    }
                }

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
                            color: Theme.panel
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

            // ---- pace ---------------------------------------------------
            ToolCard {
                title: "Pace"
                hint: "How fast the picture moves: how often it cuts, and how short or long a shot is allowed to be."

                SliderRow {
                    label: "Cutting"
                    valueText: panel.paceWord(studio.params.aggro)
                    hint: "Calm sits on a shot; frenetic cuts on nearly every beat."
                    from: 0
                    to: 2
                    step: 0.05
                    value: studio.params.aggro
                    onMoved: (v) => studio.setParam("aggro", v)
                }

                SliderRow {
                    label: "Shortest shot"
                    valueText: panel.seconds(studio.params.minShotSec)
                    hint: "Quiet passages never go below this, however busy the song is."
                    from: 0.2
                    to: 4
                    step: 0.1
                    value: studio.params.minShotSec
                    onMoved: (v) => studio.setParam("minShotSec", v)
                }

                SliderRow {
                    label: "Longest hold"
                    valueText: panel.holdText(studio.params.holdMax)
                    hint: "A single clip is never left on screen longer than this."
                    from: 1
                    to: 8
                    step: 1
                    value: studio.params.holdMax
                    onMoved: (v) => studio.setParam("holdMax", v)
                }

                SwitchRow {
                    label: "Follow the song"
                    hint: "Quiet parts hold longer, loud parts cut faster, and a drop opens on a wide shot."
                    checked: studio.params.mood === true
                    onToggled: (on) => studio.setParam("mood", on)
                }
            }

            // ---- cuts and transitions -----------------------------------
            ToolCard {
                title: "Cuts & transitions"
                hint: "What happens at the join, and whether the drop is chopped into a burst."

                SliderRow {
                    label: "Between shots"
                    valueText: panel.fadeText(studio.params.fadeMs)
                    from: 0
                    to: 500
                    step: 10
                    value: studio.params.fadeMs
                    onMoved: (v) => studio.setParam("fadeMs", v)
                }

                // The two settings behind `[` and `]` on a single cut, so the
                // per-cut control is not a fixed 40 ms step nobody chose.
                SliderRow {
                    label: "One cut, per step"
                    valueText: Math.round(studio.params.cutStep) + " ms"
                    hint: "How much [ and ] move a single cut."
                    from: 10
                    to: 100
                    step: 5
                    value: studio.params.cutStep
                    onMoved: (v) => studio.setParam("cutStep", v)
                }
                SliderRow {
                    label: "Longest single cut"
                    valueText: Math.round(studio.params.cutMax) + " ms"
                    hint: "No one join dissolves for longer than this."
                    from: 100
                    to: 1000
                    step: 50
                    value: studio.params.cutMax
                    onMoved: (v) => studio.setParam("cutMax", v)
                }

                SwitchRow {
                    label: "Stutter the drops"
                    hint: "Chops the drop into a quick burst of shots instead of one."
                    checked: studio.params.stutter === true
                    onToggled: (on) => studio.setParam("stutter", on)
                }

                SliderRow {
                    label: "Burst length"
                    visible: studio.params.stutter === true
                    valueText: Math.round(studio.params.split) + " pieces"
                    from: 2
                    to: 5
                    step: 1
                    value: studio.params.split
                    onMoved: (v) => studio.setParam("split", v)
                }

                SliderRow {
                    label: "Bursts"
                    visible: studio.params.stutter === true
                    valueText: "up to " + Math.round(studio.params.stutterMax)
                    from: 1
                    to: 24
                    step: 1
                    value: studio.params.stutterMax
                    onMoved: (v) => studio.setParam("stutterMax", v)
                }

                SwitchRow {
                    label: "Copy the track's own stutters"
                    hint: "Cuts land on the off-beat hits the song already has."
                    checked: studio.params.replicate === true
                    onToggled: (on) => studio.setParam("replicate", on)
                }
            }

            // ---- motion -------------------------------------------------
            // Ken Burns, on or off, and how far. This is a picture setting: it is
            // applied to the edit that is already on screen, not only on the next
            // Generate, and a saved look carries it.
            ToolCard {
                title: "Motion"
                hint: "Every shot slowly pushes in or pulls out, alternating so two neighbours never drift the same way."

                SwitchRow {
                    label: "Move the shots"
                    hint: "Off holds every frame perfectly still — a slideshow, not a camera move."
                    checked: studio.params.moveOn === true
                    onToggled: (on) => studio.setParam("moveOn", on)
                }

                SliderRow {
                    label: "How far"
                    visible: studio.params.moveOn === true
                    valueText: Math.round(4 * studio.params.moveAmt) + "–"
                             + Math.round(13 * studio.params.moveAmt) + " % of the frame"
                    hint: "The music decides where in that range a shot lands: loud runs move more, quiet parts barely at all."
                    from: 0.1
                    to: 1
                    step: 0.05
                    value: studio.params.moveAmt
                    onMoved: (v) => studio.setParam("moveAmt", v)
                }
            }

            // ---- footage ------------------------------------------------
            ToolCard {
                title: "Footage"
                hint: "Which clips are allowed into the edit. Everything excluded stays visible in the rail, dimmed."

                SwitchRow {
                    label: "Best clips only"
                    hint: "Keeps the highest-scoring clips and drops the rest."
                    checked: studio.filters.scoreOn === true
                    onToggled: (on) => studio.setFilter("scoreOn", on)
                }

                SliderRow {
                    label: "Keep the top"
                    visible: studio.filters.scoreOn === true
                    valueText: Math.round(studio.filters.scorePct) + "% · "
                             + studio.poolCount + " clips"
                    from: 10
                    to: 100
                    step: 5
                    value: studio.filters.scorePct
                    onMoved: (v) => studio.setFilter("scorePct", v)
                }

                SwitchRow {
                    label: "Keep the frame moving"
                    hint: "Drops stationary shots — parked cars, a camera left running."
                    checked: studio.filters.mvOn === true
                    onToggled: (on) => studio.setFilter("mvOn", on)
                }

                SliderRow {
                    label: "Movement at least"
                    visible: studio.filters.mvOn === true
                    valueText: Math.round(studio.filters.mvPct) + "%"
                    from: 10
                    to: 100
                    step: 5
                    value: studio.filters.mvPct
                    onMoved: (v) => studio.setFilter("mvPct", v)
                }

                SwitchRow {
                    label: "Skip dark clips"
                    hint: "Leaves out clips that are mostly black — tunnels, night shots."
                    checked: studio.filters.junk === true
                    onToggled: (on) => studio.setFilter("junk", on)
                }

                SwitchRow {
                    label: "Only one day"
                    hint: "Uses a single day's footage, for one trip or event."
                    checked: studio.filters.dayOn === true
                    onToggled: (on) => studio.setFilter("dayOn", on)
                }

                DayPicker {
                    Layout.fillWidth: true
                    visible: studio.filters.dayOn === true
                    onChosen: (day) => studio.setFilter("day", day)
                }

                SwitchRow {
                    label: "Only daytime"
                    hint: "Keeps the hours you shot in daylight."
                    checked: studio.filters.todOn === true
                    onToggled: (on) => studio.setFilter("todOn", on)
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: studio.filters.todOn === true
                    spacing: 10

                    SliderRow {
                        Layout.fillWidth: true
                        label: "From"
                        valueText: panel.hourText(studio.filters.tod.length ? studio.filters.tod[0].a : 8)
                        from: 0
                        to: 24
                        step: 1
                        value: studio.filters.tod.length ? studio.filters.tod[0].a : 8
                        onMoved: (v) => studio.setFilter("todFrom", v)
                    }
                    SliderRow {
                        Layout.fillWidth: true
                        label: "To"
                        valueText: panel.hourText(studio.filters.tod.length ? studio.filters.tod[0].b : 19)
                        from: 0
                        to: 24
                        step: 1
                        value: studio.filters.tod.length ? studio.filters.tod[0].b : 19
                        onMoved: (v) => studio.setFilter("todTo", v)
                    }
                }

                SwitchRow {
                    label: "Skip some dates"
                    hint: "Leaves out the dates you name — a day that rained, a bad shoot."
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
                        color: Theme.panel
                        radius: 6
                        border.color: skipField.activeFocus ? Theme.accent : Theme.line
                    }
                    onEditingFinished: studio.setFilter("skipWindows", text)
                }

                Text {
                    Layout.fillWidth: true
                    text: "Shape"
                    color: Theme.ink
                    font.pixelSize: 11
                }
                Segmented {
                    Layout.fillWidth: true
                    options: [
                        { label: "Any", value: "any" },
                        { label: "16:9", value: "16:9" },
                        { label: "9:16", value: "9:16" },
                        { label: "4:3", value: "4:3" },
                        { label: "1:1", value: "1:1" }
                    ]
                    value: panel.shapeValue()
                    onSelected: (v) => {
                        if (v === "any") studio.setFilter("arOn", false)
                        else {
                            studio.setFilter("arOn", true)
                            studio.setFilter("ar", panel.shapeRatio(v))
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    text: studio.poolCount + " of " + studio.libraryCount + " clips can be used"
                    color: Theme.faint
                    font.pixelSize: 11
                }
            }

            // ---- order --------------------------------------------------
            ToolCard {
                title: "Order"
                hint: "The order clips are walked in. Date order tells the story of the shoot; shuffling breaks it up."

                Segmented {
                    Layout.fillWidth: true
                    options: [
                        { label: "Date order", value: false },
                        { label: "Shuffle", value: true }
                    ]
                    value: studio.params.shuffle === true
                    onSelected: (v) => studio.setParam("shuffle", v)
                }

                SwitchRow {
                    label: "Repeatable edit"
                    hint: "Locks the random choices, so Generate produces exactly the same cut again."
                    checked: Math.round(studio.params.seed) !== 0
                    onToggled: (on) => studio.setParam("seed", on ? 1234 : 0)
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: Math.round(studio.params.seed) !== 0
                    spacing: 6

                    TextField {
                        id: seedField
                        Layout.fillWidth: true
                        implicitHeight: 28
                        font.pixelSize: 11
                        placeholderText: "seed"
                        text: String(Math.round(studio.params.seed))
                        color: Theme.ink
                        validator: IntValidator { bottom: 1; top: 99999 }
                        background: Rectangle {
                            color: Theme.panel
                            radius: 6
                            border.color: seedField.activeFocus ? Theme.accent : Theme.line
                        }
                        onEditingFinished: {
                            const v = parseInt(text)
                            if (v > 0) studio.setParam("seed", v)
                        }
                    }
                    SmallButton {
                        text: "New"
                        onClicked: studio.setParam("seed", 1 + Math.floor(Math.random() * 99998))
                    }
                }
            }

            // ---- playback ------------------------------------------------
            ToolCard {
                title: "Playback"
                hint: "What the preview plays while you work."

                SwitchRow {
                    label: "Play low-res proxies"
                    hint: "DJI writes a light .LRF twin next to each clip and GoPro a .LRV; they preview faster, a little softer."
                    checked: studio.params.useLrf === true
                    onToggled: (on) => studio.setParam("useLrf", on)
                }

                // The cut is identical at every setting: this only decides how big
                // a picture the preview produces, so a 4K file can still land a
                // quarter-second shot on time.
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    text: "Preview size"
                    color: Theme.ink
                    font.pixelSize: 11
                }
                Segmented {
                    objectName: "previewSize"
                    Layout.fillWidth: true
                    options: [
                        { label: "Full", value: 0 },
                        { label: "1080p", value: 1080 },
                        { label: "720p", value: 720 },
                        { label: "540p", value: 540 }
                    ]
                    value: studio.previewHeight
                    onSelected: (v) => studio.setPreviewHeight(v)
                }
                Text {
                    Layout.fillWidth: true
                    text: studio.previewHeight === 0
                          ? "Native: every frame at the file's own size."
                          : "Scaled to " + studio.previewHeight + " lines before it reaches the screen; 4K cuts stay on time."
                    color: Theme.faint
                    font.pixelSize: 10
                    lineHeight: 1.2
                    wrapMode: Text.WordWrap
                }
            }

            // ---- render -------------------------------------------------
            // What Export… produces unless it is changed for that one run, and
            // what `--render` uses on a project that has never been exported.
            ToolCard {
                title: "Render"
                hint: "What Export… writes: the frame it encodes into, the rate, and how hard the encoder works."

                Segmented {
                    objectName: "renderSize"
                    Layout.fillWidth: true
                    // The current height is always an option: a project that came
                    // with a size the presets do not cover must still show which
                    // one it has instead of an empty strip.
                    property var sizeOptions: {
                        const base = [720, 1080, 1440, 2160]
                        const h = Math.round(studio.params.exportH)
                        if (base.indexOf(h) < 0) base.push(h)
                        base.sort(function(a, b) { return a - b })
                        return base.map(function(v) {
                            return { label: v === 2160 ? "4K" : v + "p", value: v }
                        })
                    }
                    options: sizeOptions
                    value: studio.params.exportH
                    onSelected: (v) => {
                        studio.setParam("exportW", Math.round(v * 16 / 9))
                        studio.setParam("exportH", v)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: studio.params.exportW + " × " + studio.params.exportH + " · "
                          + Math.round(studio.params.exportFps) + " fps"
                    color: Theme.faint
                    font.pixelSize: 10
                }

                Segmented {
                    objectName: "renderFps"
                    Layout.fillWidth: true
                    options: [
                        { label: "24", value: 24 },
                        { label: "25", value: 25 },
                        { label: "30", value: 30 },
                        { label: "50", value: 50 },
                        { label: "60", value: 60 }
                    ]
                    value: studio.params.exportFps
                    onSelected: (v) => studio.setParam("exportFps", v)
                }

                Segmented {
                    objectName: "renderQuality"
                    Layout.fillWidth: true
                    options: [
                        { label: "Draft", value: 0 },
                        { label: "Balanced", value: 1 },
                        { label: "High", value: 2 }
                    ]
                    value: studio.params.exportQuality
                    onSelected: (v) => studio.setParam("exportQuality", v)
                }
                Text {
                    Layout.fillWidth: true
                    text: "Draft is quick to check, High is the one to keep — the cut is the same either way."
                    color: Theme.faint
                    font.pixelSize: 10
                    lineHeight: 1.2
                    wrapMode: Text.WordWrap
                }
            }

            // ---- editor -------------------------------------------------
            ToolCard {
                title: "Editor"
                hint: "How the window behaves from one session to the next."

                SwitchRow {
                    label: "Reopen the last project"
                    hint: "Off starts with the footage and the song but no project open."
                    checked: studio.params.restoreProject === true
                    onToggled: (on) => studio.setParam("restoreProject", on)
                }

                SliderRow {
                    label: "Undo steps"
                    valueText: Math.round(studio.params.undoDepth) + " steps"
                    hint: "How far back Ctrl+Z can walk before the oldest step is dropped."
                    from: 10
                    to: 300
                    step: 10
                    value: studio.params.undoDepth
                    onMoved: (v) => studio.setParam("undoDepth", v)
                }
            }

            Item { Layout.fillHeight: true; implicitHeight: 4 }
        }
    }

    // ---- collapsed ------------------------------------------------------
    Item {
        anchors.fill: parent
        visible: panel.collapsed

        Text {
            anchors.centerIn: parent
            text: "TOOLS"
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
