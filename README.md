# vibeslideshow

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square)](https://github.com/RichardLitt/standard-readme)

Desktop editor that cuts your own footage to a song and renders a beat-synced slideshow to MP4.

A native desktop editor that cuts your own footage to a song. Point it at a
folder of clips, pick a track, pick a look in plain language — and it cuts a
beat-synced slideshow you can fix by hand, shot by shot, then render to an MP4.

Qt 6 Quick + libmpv. Linux, macOS and Windows; developed on Linux.

## Table of Contents

- [Install](#install)
- [Usage](#usage)
- [Packaging](#packaging)
- [How it works](#how-it-works)
- [Three libmpv embedding invariants](#three-libmpv-embedding-invariants)
- [Files on disk](#files-on-disk)
- [CI](#ci)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)

## Install

### Dependencies

- Qt 6.5 or newer (Quick, QuickControls2, GUI) and a working OpenGL driver
- libmpv 2.x (`libmpv-dev` / `mpv-libs` / `brew install mpv`)
- `ffmpeg` and `ffprobe` on `PATH` — packaged builds ship them next to the
  executable instead, which is what makes a bundle work without a shell
  environment
- CMake 3.21+ and a C++20 compiler

### Building from source

```sh
./scripts/build.sh            # Release, into ./build
./build/vibeslideshow
```

`scripts/build.sh --debug` or `--dir build-ci` for variants. It is a thin wrapper
around `cmake -S . -B <dir> -DCMAKE_BUILD_TYPE=...` + `cmake --build`, so plain
CMake works just as well:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Install the binary, desktop entry and icon:

```sh
cmake --install build --prefix ~/.local
```

## Usage

1. **Every launch** starts with one setup popup that asks for the two things the
   app cannot work without: a song, and some footage. A remembered song and
   footage are already in it, so a returning user just presses
   **Continue**; "Not now" leaves the restored session as it was. Both rows go
   straight to the native picker and the popup comes back afterwards. Everything
   after that is automatic — the app reads the track, scores the footage and cuts
   the first edit without another click.
2. **Footage** comes in three ways: single files, several files at once, or a
   whole folder. Use **Import…** at the top of the bin (or **Project…** in the top
   bar), or drag files and folders from the file manager straight onto the bin.
   Folders are walked recursively; every video in them (`mp4`, `webm`, `mov`,
   `mkv`, `m4v`) is indexed and scored, camera proxies (DJI `.LRF`, GoPro
   `.LRV`) are paired automatically for fast cuts and never indexed as clips, and
   a clip copied into several date folders is only indexed once. Network shares
   mounted in your file manager (SMB/SFTP/NFS) work as long as you pick the
   mounted folder. The edit is generated as soon as the scan finds clips, and
   re-cut once the scores are in — and re-cut again if the footage list changes,
   since a rescan renumbers the clips the edit names.
3. **Song…** loads a track. The app finds its tempo, energy curve, sections
   (chill / build / loud / drop) and any stutter it already has, then cuts the
   first edit for you.
4. **The bin** on the left is the project's flat clip list: a search box filters
   it by name, a click puts a clip into the show at the playhead, and a
   right-click opens a menu on that clip — insert it, or remove it from the
   project. Clips the filters exclude stay visible but dimmed. Removing footage
   — a clip, or a whole import via **Project…** — takes its shots out of the
   edit and hands their time to the last shot, so the song stays covered; a
   removed clip stays out on later rescans until you import it again.
5. The panel on the right is a set of tools, each named after what it does to the
   video and each saying its effect in words instead of a raw number:

   | Tool | What it decides |
   |---|---|
   | **Track** | what the analysis heard: tempo, key, energy, danceability, mood, level |
   | **Look** | the preset to start from, and saving the current settings as one |
   | **Pace** | how often it cuts, the shortest shot, the longest hold, and whether it follows the song's sections |
   | **Cuts & transitions** | how shots join, and the stutter burst at the drop |
   | **Footage** | which clips may be used: score, movement, darkness, a single day, daytime hours, skipped dates, aspect |
   | **Order** | date order or shuffled, and whether the edit is reproducible from a seed |
   | **Playback** | whether the preview plays the low-res proxies, and the size it plays at |

   **Preview size** (in the Playback tool) caps how big a picture the preview
   produces — full, 1080, 720 or 540 lines — and applies the moment you pick it.
   Nothing about the cut changes; a 4K file simply costs less per cut, which is
   what keeps quarter-second shots landing on the beat. Vertical clips are capped
   by their long side and nothing is ever upscaled.

   Readouts are human: a hold is "up to 2.4 s" at the song's tempo, a transition
   is "quick dissolve · 120 ms", and the footage tool says how many of the
   library's clips survive the filters. The panel scrolls, and collapses to its
   title when you want the stage wider.

   The **Track** card is the analysis talking: tempo, key and mode (Krumhansl
   profile correlation over the chroma of the decoded audio), energy (loudness ×
   onset density × brightness), danceability (how strongly the onset envelope
   repeats at the beat period, and how regular the spacing is), mood (major/minor
   weighted by how bright the mix is) and level in dBFS. Nothing is looked up —
   all of it is measured from the file. The cut uses it: shots with movement land
   where the music moves and still ones where it breathes, and a track with a
   clear pulse is cut tighter than a loose one.
6. The strip at the bottom is the edit. Select a segment to shorten, lengthen,
   chop, re-roll its in-point, swap its clip or delete it — from the toolbar, or
   by right-clicking the shot itself, which selects it and opens the same list
   with the key for each operation beside it. Click a clip in the
   rail to drop it in at the playhead. It has a zoom (**−** / **+** in the
   toolbar, or Ctrl+scroll over the strip): at 1× the whole song fits the window,
   and further in the strip scrolls — with the wheel or a sideways trackpad
   swipe, by dragging with the middle button (or Shift+drag; a plain
   drag still scrubs), or by dragging the bar along the bottom. Right-drag is the
   menu's, so it no longer pans. Trackpad deltas
   are honoured in either direction, so natural scrolling behaves. Segments then get
   wide enough to show a preview of the clip they hold, and its name once there is
   room. The playhead is followed while the show plays; scrolling yourself leaves
   the view alone. Every edit lands on the stage at once: the shot the playhead is
   over is re-cued where it stands, so a swap or a chop shows straight away
   instead of waiting for the next scroll. Only when an edit does not change that
   picture — shortening some other shot, say — is the surface left untouched.

   Every edit is undoable. The two arrows left of the edit actions, `Ctrl+Z` and
   `Ctrl+Shift+Z` step back and forward through up to a hundred steps of
   shorten / lengthen / chop / swap / re-roll / remove / drop / cut change.
   Generating a new edit starts the history over; importing or removing footage
   is outside it, because a rescan renumbers the clips the edit names.

   Each shot also gets a slow move — the camera pushing in or pulling out, a
   couple of percent to about a thirteenth, chosen by whether the music is loud,
   building or chillin — alternating direction shot to shot so two neighbours
   never drift the same way. The preview plays that move live, and the render
   bakes it in. How two shots join is per cut: `H` makes the cut into the
   selected shot hard, `[` softens it and `]` sharpens it again, in 40 ms steps
   up to half a second. The cut before the first shot does not exist, so those
   three do nothing there.
7. **File…** in the top bar holds the project: **New project** (keeps the
   footage and the song, clears the edit), **Open…** and **Save a copy…**
   (`*.vibes`). A project remembers the footage, the song, the look, the filters
   and the edit shot by shot — in-points, lengths, cuts and moves — so a reopened
   project comes back cut exactly as it was saved, and the window title shows its
   name. Shots whose clip is no longer on disk are dropped on open and the status
   says how many. The last project is reopened on the next launch.
8. **Export…** renders the edit to an MP4 with ffmpeg: one encode per shot into a
   temporary directory, then a single pass that either joins the shots or
   dissolves them and muxes the song. Draft / Balanced / High change the encoder
   settings, not the cut. The dialog shows the progress of each phase and can
   stop the run; a cancelled export leaves no file behind. The output is
   1920×1080 at 30 fps, letterboxed rather than cropped, and its length is the
   edit's own length.

The same render runs without a window, which is what a batch job or a build
machine uses:

```sh
vibeslideshow --render project.vibes --out film.mp4 \
              [--quality draft|balanced|high] [--size 1920x1080]
```

It prints the progress on stderr, the finished file on stdout, and exits 0 on
success, 1 when the render fails (or the project has no edit) and 2 for bad
arguments.

While any pass runs, the top bar's message slot carries what it is doing and how
far it has got (counts, ETA, percentage), with the progress line on the top bar's
own bottom edge — it sweeps while no total is known, and takes no room when the
app is idle. Controls that cannot be trusted mid-pass are visibly disabled rather
than silently wrong: while the library is being rebuilt or the song is still
being read, playback, the edit actions, timeline scrubbing, the rail's
drop-in-a-clip click and the keyboard shortcuts are all off. Scoring only fills in
numbers, so the workspace stays usable while it runs.

The window is a three-pane editor at any size (960×620 and up). The preview is
always a 16:9 frame, as large as the stage allows and letterboxed by mpv inside
it, so a 4:3 or vertical clip never changes the shape of the window. The timeline
takes every pixel the preview does not need — widening the window makes it taller,
since the preview's height follows its width — and the rail and settings panel
scale with the window instead of holding a fixed size.

Space plays and pauses, arrow keys nudge ±5 s, `Ctrl+G` regenerates the edit,
`Esc` stops. On the edit: `-` and `+` shorten and lengthen the selected shot,
`C` chops it in two, `I` takes another moment from the same clip, `S` swaps in a
different clip, `Delete` removes it, and `Shift+R` re-rolls every in-point. The
cut into the selected shot: `H` hard, `[` softer, `]` sharper. `Ctrl+Z` undoes,
`Ctrl+Shift+Z` redoes. The keys stand down while a text field has focus, and
while the workspace is locked.

## Packaging

`scripts/package.sh` builds and bundles for the host OS into `dist/`:

| Platform | Produces | Needs |
|---|---|---|
| Linux | `vibeslideshow-<ver>-linux-<arch>.tar.gz`, plus `.deb` when `dpkg-deb` exists | Qt ≥ 6.5, libmpv, ffmpeg |
| macOS | `vibeslideshow-<ver>-macos.zip` and `.dmg` (Qt + libmpv inside the `.app`) | `brew install qt mpv ffmpeg dylibbundler` |
| Windows | `vibeslideshow-<ver>-windows-x86_64.zip` (Qt, libmpv, ffmpeg inside) | MSYS2 UCRT64 shell with `qt6-base qt6-declarative mpv ffmpeg` |

`--no-build` packages an existing `./build`. The platform scripts are
`scripts/package-{linux,macos,windows}.sh` and can be run directly.

Both bundles put `ffmpeg`/`ffprobe` beside the executable: an app started from the
Finder, the Start menu or a `.desktop` file never inherits the user's `PATH`, so
the code looks next to the binary first (and honours `VIBESLIDESHOW_TOOLS_DIR`).

## How it works

```
src/
  core/Analyzer    ffmpeg decode, beats, sections, and track features
                   (key/mode, energy, danceability, brightness, loudness)
  core/Scheduler   buildCuts() + materialise() + seeded RNG
  core/Params      the generation parameters, presets, filter + skip-window config
  core/Library     scan, ffprobe, proxy pairing (.LRF/.LRV), scoring, cache
  core/Document    the .vibes project file: read and write
  core/RenderPlan  the edit as render segments (paths, in-points, moves)
  core/Exporter    ffmpeg: one encode per shot, then join or dissolve + mux
  player/MpvItem   one video surface: QQuickFramebufferObject + mpv_render_gl
  player/Show      the edit engine: audio-master clock, preload, crossfade
  ui/ClipModel     list model over the library, filter state, thumbnails
  ui/TimelineModel segment list model, the one command table, every edit operation
  ui/Studio        the single QML-facing controller; persistence
qml/               Studio (window), LibraryRail, Stage, SidePanel, Timeline +
                   ClipMenu, and the widgets they are built from
```

The clock is an **audio-only third mpv instance**. A cut can never perturb the
song timeline, because video surfaces never carry the soundtrack.

The song is the clock, but it is not allowed to run ahead of the picture: when the
visible clip stops producing frames, the song is paused at that position, a
"Buffering…" badge appears, and playback resumes only once the surface has media
buffered ahead (and has been re-synced with a single seek if it drifted). Seeking
goes through the same gate, so sound and image always restart together — on a slow
network share the show waits instead of drifting out of sync.

Four video surfaces are kept, and the edit is buffered ahead of the playhead.
One surface shows the current shot; the other three hold the next three shots,
already open, seeked to their in-point and paused on a decoded frame. A cut is
then a reveal, not a seek: the surface that already holds the shot is put on
screen and unpaused. That is what makes short cuts work at all — a quarter-second
shot has no time to open a file, but it has plenty of time to swap two surfaces.
The lookahead is filled whenever the edit, the playhead or the cut changes,
including while paused, so pressing play after a scrub starts warm.

A warming surface is only asked for its own shot plus a little readahead; the
surface that is playing gets the full one. A slot is reused only when it holds a
shot the lookahead no longer needs, and a surface still crossfading out of view is
never reused — the fill comes back for it once the fade is over.

Threading rule: workers (folder scan, scoring, song analysis, thumbnails) only
ever run free functions over a snapshot and hand results back with a queued
call. Every indexed list is owned and mutated on the GUI thread alone, and each
pass carries a generation number so a superseded scan can never install stale
results.

## Three libmpv embedding invariants

These cost real debugging time; do not "clean them up".

1. **Force the OpenGL RHI** (`QQuickWindow::setGraphicsApi` before
   `QGuiApplication`). With Vulkan the framebuffer object's content never
   composites — the app looks like it is rendering black video.
2. **Bind the item's own FBO explicitly** and pass `int(fbo->handle())` with the
   FBO's device-pixel size. `defaultFramebufferObject()` returns 0 here, which
   makes mpv draw into nothing. The FBO itself is owned by Qt; never delete it.
3. **Defer `loadfile` until the render context exists.** Loading from the QML
   thread before `mpv_render_context_create` gives `Video: no video` and a
   permanently black surface.

`setlocale(LC_NUMERIC, "C")` must be called *after* `QGuiApplication` is
constructed: Qt's constructor calls `setlocale(LC_ALL, "")` and would undo an
earlier call, and mpv refuses to run under a comma-decimal locale.

## Files on disk

- `~/.config/vibeslideshow/params.json` — folders, song, look, filters, and the
  project last opened
- `~/.config/vibeslideshow/profiles.json` — saved looks
- `<somewhere>.vibes` — a project, wherever you save it: footage, song, look,
  filters and the edit
- `<footage folder>/.vibeslideshow-score-cache.json` — everything the scan learnt
  about each clip: duration and dimensions from the probe, plus its score and
  motion, keyed by byte size + mtime. A rescan of an unchanged folder opens no
  video at all (`~/.cache/vibeslideshow/` when the folder is read-only)
- `~/.cache/vibeslideshow/thumbs/` — rail and timeline thumbnails

## CI

`.github/workflows/build.yml` builds and packages all three platforms on every
push and pull request, and attaches the artifacts to a GitHub release when a `v*`
tag is pushed:

- **linux** — Ubuntu 24.04, Qt from `install-qt-action` (the distro still ships
  6.4), `libmpv-dev` from apt, headless start-up smoke test, tarball + `.deb`
- **macos** — `macos-14`, Homebrew Qt/mpv/ffmpeg, `macdeployqt` bundle, zip + dmg
- **windows** — MSYS2 **UCRT64** (the environment mpv is still built for) with Qt 6,
  libmpv and ffmpeg, `windeployqt` bundle, zip

## Maintainers

- Tamás Vince ([@nemvince](https://github.com/nemvince))

## Contributing

Questions and bug reports go to the [issue
tracker](https://github.com/nemvince/vibeslideshow/issues). Pull requests are
welcome — run `./scripts/build.sh` and the packaged bundle for your platform
before opening one, and leave the three libmpv invariants above alone unless you
can reproduce the failure they prevent.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). The app links Qt (LGPL-3.0) and
libmpv (LGPL-2.1+ / GPL-2.0+ depending on the build); GPL-3.0 is compatible with
both. © Tamás Vince.
