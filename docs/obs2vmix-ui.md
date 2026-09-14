# obs2vmix — the switcher window

obs2vmix keeps the OBS engine (libobs, every source, filter, output and transition) and
replaces the operator's window with a vision-mixer layout: a vMix / ATEM way of switching
scenes, laid out like an Avid source/record pair.

Interactive mockup, the reference for every decision below:
https://claude.ai/code/artifact/2701246d-0dc3-48cf-bea8-3dfd47140d03

## The window (since 14 Sep 2026: zero text)

Marko, 14.9.2026: "a very minimalistic user interface with zero text. There is no text at all.
I just see images and previews. Everything is done through the right mouse button."

```
┌──────────────────────────────────┬───────────────────────────────────┐
│ ━━━━━━━━━━━━ green ━━━━━━━━━━━━━ │ ━━━━━━━━━━━━━ red ━━━━━━━━━━━━━━━ │
│                                  │                                   │
│   SOURCE  (preview)              │   FINAL  (program)                │
│                                  │                                   │
├──────────────────────────────────┴───────────────────────────────────┤
│ [ thumb ] [ thumb ] [ thumb ] [ thumb ] [ thumb ]  ‹ scroll for more ›│
└──────────────────────────────────────────────────────────────────────┘
```

* **Two monitors and the strip, nothing else.** Source (the preview) on the left, Final (the
  program) on the right, a two-pixel tally line over each (green, red), the five live
  thumbnails under them. No labels, no buttons, no bars, no names.
* **Three panes, lines between them** (0.4.1, Marko: "same as in OBS normal"): a draggable
  line between Source and Final, another between the monitors and the strip; the sizes are
  remembered (`obs2vmix/MonitorSplit`, `obs2vmix/PaneSplit`). Both monitors always fit their
  pane, as in Resolve or Avid: no zoom, no scrollbars, no zoom bar (zooming is a later task).
* **Every right-click menu says which window it is** in its first line: `SOURCE · Cam 1`,
  `FINAL · Cam 1`, `3 · Cam 2` on a thumbnail.
* **Right-click on Source** → Take (Space), Cut (Enter), Edit scene…, Record this scene,
  the projector, a screenshot; then One monitor, Fullscreen, OBS Studio.
* **Right-click on Final** → **Transition…** (a small dialog: the transition, its length, the
  unit frames / seconds / ms; OK stores it and Space uses it from then on), **Audio effects…**
  (the FX rack as a floating window, see below), Transition options (OBS's duplicate / edit
  properties / swap), Record and Stream (the main outputs), the projector, a screenshot; then
  One monitor, Fullscreen, OBS Studio.
* **Right-click on a thumbnail** → Load into Source, Take this scene, Edit scene… (Close the
  scene editor while it is open), Record this scene.
* **Hover a thumbnail** → its number and name in a tooltip, and the recording line while it
  records.
* **Click** a thumbnail → it loads into Source. **Space** → Take: Source goes to Final
  through the chosen transition, and the old Final becomes the new Source (ATEM flip-flop).
  Enter = Cut. Number keys 1–9 load a scene into Source. The keys are quiet while a text
  field or a button has the focus.
* **Double-click** a thumbnail → the scene editor opens in the monitors' place: the scene's
  sources, add source, overlay, filters, properties — OBS's own editing, unchanged. Esc, or
  the thumbnail's menu, closes it.
* **One monitor** (in either menu) → one monitor only, DaVinci style; double-click the
  monitor to swap between Source and Final.
* Green frame on a thumbnail = on Source, red = on Final, grey = being edited.

The Mode menu in the menu bar still switches between the vMix view and OBS Studio as it is;
the same entry is at the bottom of both monitor menus.

## Record a scene

Any scene records in the background to its own file, whether or not it is on Source or
Final, and any number of scenes can record at once. *Record this scene* in the thumbnail's
menu (or in the Source menu, for the scene on Source) starts it; the same entry, now ticked,
stops it.

* **The light and the time.** One reserved line under every thumbnail, empty until the scene
  records; then a round red light, blinking, and next to it the time recorded, `12:41`
  (`1:02:05` past an hour). Amber when frames drop or the disk runs low. When the disk is
  full the output stops itself and the line empties.
* **The line.** The tooltip of the thumbnail shows `00:12:41 (412 GB · 50 fps)`: elapsed time,
  then the free space on the recording disk and the frame rate the encoder is achieving;
  `· 12 dropped` is added when frames are skipped.
* **Files** go to the normal OBS recording folder, named `<scene> <date> <time>.<ext>`, with
  the recording encoder and container from Settings → Output. Audio is the main mix (track 1)
  — a scene has no audio of its own; the operator picks the track in Settings.
* **Cost.** Each recording is a full extra render and encode of that scene. Two or three run
  comfortably with a hardware encoder; from the fourth on, the dot turns amber first.

## FX rack

The audio chain that runs before the output. *Audio effects…* in the Final monitor's menu
opens it as a floating window, Blue Cat PatchWork style: eight slots in signal order, each
with an on/off button, the plugin selector (every VST 2.x plugin found in the OBS VST
folders), the plugin's own preset list, a **UI** button opening the plugin's own window, and
a **mix** knob (dry/wet); ▲▼ reorder. Every change is stored as it is made; **OK** closes the
window, or it stays open and floats over the switcher for as long as the operator wants it.

* **Apply to.** `Live` = the chain is heard on the stream only; the recording stays dry.
  `Live + Record` = the recording gets it too. `Bypass rack` mutes the whole chain without
  losing anything.
* **Rack presets.** The whole rack — plugins, order, on/off, mix, and each plugin's state —
  saves under a name (`Save`, `Save as…`) and reloads from the dropdown. Racks are files in
  the profile, so they travel with it.
* **Formats.** OBS hosts **VST 2.x only** (`.vst` bundles on macOS, `.dll` on Windows, `.so` on
  Linux) through its own `obs-vst` host, no Steinberg SDK. VST3 and AU are not supported
  anywhere in the tree and are out of scope; the rack shows whatever `obs-vst` can load.

## MIDI learn

The rack is controlled from a MIDI surface the way Ableton Live does it: **MIDI** button →
learn mode, every mappable control gets a blue outline → click one → touch a knob or pad on
the controller → linked, the control shows `CC 21` (or `N 36`). Press MIDI again to leave
learn mode. Mappable: every slot's on/off and mix, the Live / Live + Record switch, Bypass.
Mappings save with the profile, not with the rack preset, so one controller layout drives
every rack. OBS has no MIDI in the tree; obs2vmix adds a small input layer (see below).

## How it maps onto OBS

OBS already has most of the machinery. Studio Mode *is* preview/program; we change the
layout and add the strip, not the engine.

| Need | Where in the tree | What changes |
|---|---|---|
| Preview + Program side by side | `frontend/widgets/OBSBasic_StudioMode.cpp` (`SetPreviewProgramMode`, `programWidget`, `programLayout`) | Studio Mode becomes the only mode; zero spacing; no labels, a two-pixel tally line over each monitor |
| Transition dropdown + duration | `OBSBasic_StudioMode.cpp` (`ShowTransitionDialog`) | A dialog from the Final monitor's menu: combo, length, unit (frames/seconds/ms, the unit remembered in `obs2vmix/DurationUnit`); frames ↔ ms via `obs_get_video_info().fps_num/fps_den` |
| Take on Space | `frontend/widgets/OBSBasic_Hotkeys.cpp`, `TransitionClicked()` | Default hotkey Space for the studio-mode transition; ignored while a text field has focus |
| Scene thumbnails | `frontend/components/SceneStrip.{cpp,hpp}`, modelled on `frontend/components/Multiview.cpp` | One `OBSQTDisplay` renders every scene into a row with `obs_source_video_render`, scrolled by an offset; no widgets under it: the name is a tooltip, the actions a right-click menu (`SceneMenuRequested`), the recording state a dot drawn in the corner |
| Scene editor | existing sources dock + `OBSBasicPreview` | Opening = select scene for editing, show sources dock + preview in the monitors' area; closing restores the monitors |
| Collapse to one monitor | `OBSBasic_StudioMode.cpp` | Hide `program` or `preview` widget, keep the other at full width |
| Record a scene | new `frontend/utility/SceneRecorder.{cpp,hpp}`; API in `libobs/obs.h` (`obs_canvas_*`) | One `obs_canvas_create(name, &ovi, PROGRAM)` per recording with the scene in channel 0 (`obs_canvas_set_channel`); an `ffmpeg_muxer` output (as `SimpleOutput.cpp` builds `fileOutput`) whose video encoder is bound to `obs_canvas_get_video(canvas)` with `obs_encoder_set_video`, audio encoder to `obs_get_audio()`; `obs_output_set_media(output, canvas video, obs_get_audio())`; `path` = recording folder + scene name |
| Status line (tooltip + dot) | `SceneStrip`, timer at 4 Hz, modelled on `frontend/widgets/OBSBasicStats.cpp` | elapsed = `obs_output_get_total_frames / fps`; free disk = `os_get_free_disk_space(path)` (`platform.h`, same as `OBSBasic::LowDiskSpace`); fps = Δ`video_output_get_total_frames(canvas video)` per second; dropped = `video_output_get_skipped_frames(canvas video)` (encoder lag) + `obs_get_lagged_frames()` (render lag, global) — *not* `obs_output_get_frames_dropped`, which counts network drops |
| FX rack, the chain | `plugins/obs-vst` (`VSTPlugin.cpp`, `vst_filter` with `filter_audio`) | Host each slot with the existing `VSTPlugin` class (load, `effSetSampleRate`/`effSetBlockSize`, `getParameter`/`setParameter`, `effGetChunk`/`effSetChunk` for presets, `EditorWidget` for the UI) but *outside* the filter: a `MasterChain` that runs the slots in order over a float buffer |
| FX rack, where it runs | `libobs/obs-audio.c` `audio_callback()` after the *mix audio* loop, before `discard_audio` | New libobs hook `obs_set_master_audio_processor(cb, param)` called with `mixes[track].data` per track. Live = process track 1 only; Live + Record = also every track the recording output uses (`Settings → Output → Recording → Audio Track`). Bypass = hook installed, chain skipped |
| Rack window | `frontend/components/FxRack.{cpp,hpp}` | A `QDialog` with `Qt::Tool`, opened by `FxRack::Open()` from the Final menu: eight slot rows, `QComboBox` filled from `obs-vst`'s scan, OK at the foot |
| Rack presets | `<profile>/racks/<name>.json` | `obs_data_t` with slots [plugin path, on, mix, chunk base64]; dropdown lists the folder |
| MIDI | new `frontend/utility/MidiIn.{cpp,hpp}` on **libremidi** (header-friendly, CoreMIDI / WinMM / ALSA, added under `deps/`) | Input thread → `QueuedConnection` to the rack; `MidiMap` = `{cc|note, number} → control id`, saved as `<profile>/midi.json`; learn mode arms one control and takes the next message |
| Registration of new files | `frontend/cmake/ui-components.cmake` | Add SceneStrip, FxRack, SceneRecorder, MidiIn sources; `libobs/CMakeLists.txt` for the hook |
| Strings | `frontend/data/locale/en-US.ini` | Source, Record, Take, Cut, frames, seconds |

## Build

There is no local toolchain: the source is edited file-by-file with `surgeon` and compiled
by GitHub Actions.

* Push to `master` → upstream `push.yaml` runs the full build and **packages**: macOS DMG
  (arm64 + x86_64), Windows installer + zip, Ubuntu deb, Flatpak. Downloads under
  *Actions → run → Artifacts*.
* Push to any `obs2vmix/**` branch → `obs2vmix-branches.yaml` runs the same build, so
  work-in-progress is compiled before it reaches master.
* Unsigned macOS builds: right-click → Open on first launch, or `xattr -dr com.apple.quarantine`.

## Phases

1. **Build loop** — CI green on the untouched fork. *(done)*
2. **Transition seam** — dropdown + length on the seam, unit selector, Space = Take, Enter = Cut. *(done)*
3. **Scene strip** — `frontend/components/SceneStrip.{cpp,hpp}`: one display renders five live
   thumbnails with tally frames, names under them, wheel / scrollbar for more, click → Source,
   double-click → editor, 1–9 → Source. *(done 13 Sep 2026)*
4. **Editor + collapse** — double-click: Source alone at full width with the Sources dock, *Done*
   or Esc closes; ⧉ collapses to one monitor, double-click the monitor to swap. *(done)*
5. **Identity** — the app is **OBS2vMix**: bundle `OBS2vMix.app`, display name, Windows version
   resource, Linux desktop entry, tray, title bar, About, and a new icon everywhere (asset
   catalog, DMG volume icon, `.ico`, Linux PNG/SVG, window and menu-bar icons). The bundle
   identifier and the config folder stay OBS Studio's, so it shares scenes and profiles with an
   installed OBS. The app's own version comes from the newest `obs2vmix-<x.y.z>` git tag
   (`OBS2VMIX_VERSION`), shown as `OBS2vMix 0.3.0 (OBS 32.2.2)`; the OBS project version is no
   longer touched by those tags (`git describe --exclude "obs2vmix-*"`). *(done)*
6. **Record a scene** — `frontend/utility/SceneRecorder.{cpp,hpp}`: an `obs_view` with the scene
   in channel 0 and its own video mix (`obs_view_add2`), the recording encoder from
   Settings → Output (simple: the same quality rules as the simple output; advanced:
   `recordEncoder.json`), AAC on the main mix, `ffmpeg_muxer` (or `mp4_output` / `mov_output`
   for hybrid MP4/MOV), file `<scene> <date> <time>.<ext>` in the recording folder. The status
   line polls at 4 Hz: elapsed, free disk, encoded fps, skipped + lagged frames; amber on drops,
   red under 1 GB free. *(done)*
7. **FX rack** — libobs gets `obs_set_master_audio_processor()` (`libobs/obs-audio.c`), called
   with every mixed track after the mix and before the outputs. `frontend/utility/MasterChain`
   runs eight slots over it, hosted by obs-vst's `VSTPlugin` class built into a static library
   from `plugins/obs-vst` (`frontend/cmake/feature-obs2vmix.cmake`), with try-lock on the audio
   thread so a loading plugin never stalls it. `frontend/components/FxRack` is the skyscraper
   line under the Record monitor and the panel under the monitors: plugin, its own presets, UI,
   mix, on/off, ▲▼ to reorder (buttons instead of drag), Live / Live + Record, Bypass, rack
   presets `<profile>/racks/<name>.json`, the live rack in `<profile>/obs2vmix-rack.json`.
   *Live* means track 1: in the simple output mode the recording also uses track 1, so it is
   not dry there — that is OBS's track model. *(done)*
8. **MIDI learn** — `frontend/utility/MidiIn` is a small input layer of its own (CoreMIDI with
   the MIDI 1.0 protocol, WinMM, ALSA sequencer) instead of libremidi: every input is opened,
   CC and note-on arrive as Qt signals. MIDI button → learn, click a control, touch the
   controller; mappings in `<profile>/obs2vmix-midi.json`, shown as `CC 21` / `N 36` badges.
   *(done)*

Phases 6–8 were added 13 Sep 2026 from Marko's spec of the same day; the mockup shows all three.
Phases 3–8 were built 13 Sep 2026 on `obs2vmix/seam`.
14 Sep 2026, afternoon, **zero text**: the seam bar (Source / Record tags, transition combo,
CUT, TAKE, collapse, Done, OBS), the strip's header and hint, the names and record circles
under the thumbnails, the FX summary line and the labels over the monitors are all gone.
What is on the screen: two monitors with a tally line each, five thumbnails. Every action
moved to the right mouse button (Source menu, Final menu, thumbnail menu); the transition is
a dialog with OK, the FX rack a floating window with OK. Version obs2vmix-0.4.0. The same
afternoon, 0.4.1, from his first look: the Source monitor had come up in OBS's zoomed mode
with a zoom bar under it ("I lost my preview window"), so the switcher locks it to fit; the
three panes got draggable lines; every menu names its window; the recording light and time
came back under the thumbnails. Builds are Apple Silicon only from 0.4.0 on.

14 Sep 2026: Studio Mode is the only mode. The default is on, startup forces it on whatever an older
OBS config saved, and the View menu toggle and its hotkey cannot switch it off; without this a
profile with Studio Mode off showed plain OBS and none of the switcher (Marko: "the user interface
of OBS is persistent").

## The release ritual (14 Sep 2026)

Marko: "when a new Mac version is ready you download and update automatically from this chat and
inform me." So after every change:

1. Tag **before** pushing: `git tag obs2vmix-x.y.z && git push origin obs2vmix/seam --tags`. The
   build reads the newest reachable tag into `OBS2VMIX_VERSION`; a tag created afterwards by the
   release workflow is one version late.
2. Watch the `obs2vmix branch build` run; all seven jobs green.
3. `gh workflow run obs2vmix-release.yaml --ref obs2vmix/seam -f tag=obs2vmix-x.y.z -f run_id=<run>`.
4. On this Mac: `obs2vmix-update` (downloads, installs, strips the quarantine flag, opens).
5. `whatsapp "OBS2vMix x.y.z is ready and installed: <release url>"`.

In the app: Help → *Update OBS2vMix…* runs the same updater in the Terminal (fetched fresh from
`tools/obs2vmix-update`), Help → *Check for a new OBS2vMix* asks GitHub, and at startup a newer
release is offered (config `obs2vmix/CheckUpdates`).
