# vibeslideshow

A native desktop editor that cuts your own footage to a song. Point it at a
folder of clips, pick a track, pick a look in plain language — and it cuts a
beat-synced slideshow you can fix by hand, shot by shot.

Qt 6 Quick + libmpv. Linux, macOS and Windows; developed on Linux.

## Requirements

- Qt 6.5 or newer (Quick, QuickControls2, GUI) and a working OpenGL driver
- libmpv 2.x (`libmpv-dev` / `mpv-libs` / `brew install mpv`)
- `ffmpeg` and `ffprobe` on `PATH` — packaged builds ship them next to the
  executable instead, which is what makes a bundle work without a shell
  environment
- CMake 3.21+ and a C++20 compiler

## Build and run

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

## Packaging

`scripts/package.sh` builds and bundles for the host OS into `dist/`:

| Platform | Produces | Needs |
|---|---|---|
| Linux | `vibeslideshow-<ver>-linux-<arch>.tar.gz`, plus `.deb` when `dpkg-deb` exists | Qt ≥ 6.5, libmpv, ffmpeg |
| macOS | `vibeslideshow-<ver>-macos.zip` and `.dmg` (Qt + libmpv inside the `.app`) | `brew install qt mpv ffmpeg dylibbundler` |
| Windows | `vibeslideshow-<ver>-windows-x86_64.zip` (Qt, libmpv, ffmpeg inside) | MSYS2 MINGW64 shell with `qt6-base qt6-declarative mpv ffmpeg` |

`--no-build` packages an existing `./build`. The platform scripts are
`scripts/package-{linux,macos,windows}.sh` and can be run directly.

Both bundles put `ffmpeg`/`ffprobe` beside the executable: an app started from the
Finder, the Start menu or a `.desktop` file never inherits the user's `PATH`, so
the code looks next to the binary first (and honours `VIBESLIDESHOW_TOOLS_DIR`).

## Using it

1. **Footage…** in the top bar lists the folders the library scans: add folders,
   remove the ones you are done with. Every video in them (`mp4`, `webm`, `mov`,
   `mkv`, `m4v`) is indexed and scored; DJI `.LRF` proxies are paired
   automatically for fast cuts and never indexed as clips, and a clip copied into
   several date folders is only indexed once. Network shares mounted in your file
   manager (SMB/SFTP/NFS) work as long as you pick the mounted folder.
   Probing and scoring run in parallel and the top bar shows progress with an
   ETA; the edit is generated as soon as the scan finds clips, and re-cut once
   the scores are in.
2. **Song…** loads a track. The app finds its tempo, energy curve, sections
   (chill / build / loud / drop) and any stutter it already has, then cuts the
   first edit for you.
3. The rail on the left shows every clip with a score; clips the filters exclude
   stay visible but dimmed.
4. The panel on the right is the settings panel, in collapsible sections: **Look**
   (the one preset manager: built-in looks, saved looks with a delete button, and
   saving under a name), **Footage** (the filters — best
   clips only, keep the frame moving, skip dark clips, one day, daytime hours),
   **Saved looks** and **Advanced** (a dozen controls that mean something;
   thresholds and windows the presets tune stay out of the UI).
5. The strip at the bottom is the edit. Select a segment to shorten, lengthen,
   chop, re-roll its in-point, swap its clip or delete it. Click a clip in the
   rail to drop it in at the playhead.

Space plays and pauses, arrow keys nudge ±5 s, `Ctrl+G` regenerates the edit,
`Esc` stops.

## How it works

```
src/
  core/Analyzer    ffmpeg decode + beat/energy/section analysis
  core/Scheduler   buildCuts() + materialise() + seeded RNG
  core/Params      the generation parameters, presets, filter + skip-window config
  core/Library     scan, ffprobe, LRF pairing, scoring, cache
  player/MpvItem   one video surface: QQuickFramebufferObject + mpv_render_gl
  player/Show      the edit engine: audio-master clock, preload, crossfade
  ui/ClipModel     list model over the library, filter state, thumbnails
  ui/TimelineModel segment list model and every edit operation
  ui/Studio        the single QML-facing controller; persistence
qml/               Studio (window), LibraryRail, Stage, SidePanel + widgets
```

The clock is an **audio-only third mpv instance**. A cut can never perturb the
song timeline, because video surfaces never carry the soundtrack.

The song is the clock, but it is not allowed to run ahead of the picture: when the
visible clip stops producing frames, the song is paused at that position, a
"Buffering…" badge appears, and playback resumes only once the surface has media
buffered ahead (and has been re-synced with a single seek if it drifted). Seeking
goes through the same gate, so sound and image always restart together — on a slow
network share the show waits instead of drifting out of sync.

Two video surfaces are kept. The incoming clip is loaded and paused on its first
frame on the hidden surface once the previous crossfade is over, then revealed
when the audio clock crosses the cut: a cut is a pre-decoded reveal, not a seek.

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

- `~/.config/vibeslideshow/params.json` — folders, song, look, filters
- `~/.config/vibeslideshow/profiles.json` — saved looks
- `<footage folder>/.vibeslideshow-score-cache.json` — clip scores, so a rescan
  is free (`~/.cache/vibeslideshow/` when the folder is read-only)
- `~/.cache/vibeslideshow/thumbs/` — rail and timeline thumbnails

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). The app links Qt (LGPL-3.0) and
libmpv (LGPL-2.1+ / GPL-2.0+ depending on the build); GPL-3.0 is compatible with
both.

## CI

`.github/workflows/build.yml` builds and packages all three platforms on every
push and pull request, and attaches the artifacts to a GitHub release when a `v*`
tag is pushed:

- **linux** — Ubuntu 24.04, Qt from `install-qt-action` (the distro still ships
  6.4), `libmpv-dev` from apt, headless start-up smoke test, tarball + `.deb`
- **macos** — `macos-14`, Homebrew Qt/mpv/ffmpeg, `macdeployqt` bundle, zip + dmg
- **windows** — MSYS2 MINGW64 with Qt 6, libmpv and ffmpeg, `windeployqt` bundle, zip
