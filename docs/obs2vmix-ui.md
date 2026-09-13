# obs2vmix — the switcher window

obs2vmix keeps the OBS engine (libobs, every source, filter, output and transition) and
replaces the operator's window with a vision-mixer layout: a vMix / ATEM way of switching
scenes, laid out like an Avid source/record pair.

Interactive mockup, the reference for every decision below:
https://claude.ai/code/artifact/2701246d-0dc3-48cf-bea8-3dfd47140d03

## The window

```
┌──────────────────────────────────────────────────────────────────────┐
│ Source ⧉                 [Transition ▾][ 25 ][frames ▾] CUT TAKE   Record│
├──────────────────────────────────┬───────────────────────────────────┤
│                                  │                                   │
│   SOURCE  (preview, green)       │   RECORD  (program, red)          │
│                                  │                                   │
├──────────────────────────────────┼───────────────────────────────────┤
│ Source ▮▮▮▮▮▮▮▮▮▯▯▯  (meter)     │ FX [G][E][C][·][·][·][·][·]  Live ▾│
├──────────────────────────────────┴───────────────────────────────────┤
│ Scenes                                       1–9 preview · space take│
│ [ thumb ] [ thumb ] [ thumb ] [ thumb ] [ thumb ]  ‹ scroll for more ›│
│ ○ Intro   ● Cam 1   ○ Cam 2   ○ Slides  ○ 2-shot                     │
│         00:12:41 (412 GB · 50 fps)                                   │
└──────────────────────────────────────────────────────────────────────┘
```

* **Source and Record touch.** No gap; one hairline seam. The window is resizable and the
  two monitors share the width equally, 16:9 each.
* **Transition on the seam.** Centered above the seam: transition dropdown (the vMix list:
  Cut, Fade, Zoom, Wipe, Slide, Fly, CrossZoom, FlyRotate, Cube, CubeZoom, VerticalWipe,
  VerticalSlide, Merge, Stinger), a length box, and a unit dropdown (frames / seconds / ms).
  The engine works in milliseconds; frames convert at the output frame rate.
* **Scene strip instead of a timeline.** Exactly five thumbnails visible, name under each,
  horizontal scroll when there are more. Green frame = on Source, red frame = on Record.
* **Click** a thumbnail → it loads into Source. **Space** → Take: Source goes to Record
  through the chosen transition, and the old Record becomes the new Source (ATEM flip-flop).
  Enter = Cut. Number keys 1–9 load a scene into Source.
* **Double-click** a thumbnail → the scene editor opens in the monitors' place: the scene's
  sources, add source, overlay, filters, properties — OBS's own editing, unchanged. Closing
  it collapses back into the thumbnail.
* **Collapse (⧉)** → one monitor only, DaVinci style; double-click the monitor to swap
  between Source and Record.

## Record a scene

Every thumbnail has a small red circle to the left of its name: a record button for that
scene alone. It records the scene in the background to its own file, whether or not the
scene is on Source or Record, and any number of scenes can record at once.

* **Click the circle** → recording starts and the circle fills red (blinking). Click again →
  it stops. The click does not load the scene into Source; the thumbnail still does that.
* **Status line.** While a scene records, a second line the height of the title opens under
  the name: `00:12:41 (412 GB · 50 fps)` — elapsed time, then in parentheses the free space
  on the recording disk and the frame rate the encoder is actually achieving. The line is
  reserved for every thumbnail and left empty when idle, so the strip never jumps.
* **Dropped frames.** When frames are skipped the line turns amber and says how many:
  `00:12:41 (412 GB · 47 fps · 12 dropped)`. Free space under the OBS limit turns the line red;
  when the disk is full the output stops itself and the circle empties.
* **Files** go to the normal OBS recording folder, named `<scene> <date> <time>.<ext>`, with
  the recording encoder and container from Settings → Output. Audio is the main mix (track 1)
  — a scene has no audio of its own; the operator picks the track in Settings.
* **Cost.** Each recording is a full extra render and encode of that scene. Two or three run
  comfortably with a hardware encoder; from the fourth on, the status line shows drops first.

## FX rack

A thin line under the Record monitor is the audio chain that runs before the output: the
skyscraper. One small window per slot, each showing only the first letter of the plugin in
it (`·` when empty), a chip at the right saying `Live`, `Live + Rec` or `Bypass`.

* **Click the line** → the rack expands beneath it, Blue Cat PatchWork style: eight slots in
  signal order, each with an on/off button, the plugin selector (every VST 2.x plugin found in
  the OBS VST folders), the plugin's own preset list, a **UI** button opening the plugin's
  own window, and a **mix** knob (dry/wet). Drag a slot to reorder. Click the line again to
  collapse; the letters stay as the summary.
* **Apply to.** `Live` = the chain is heard on the stream only; the recording stays dry.
  `Live + Record` = the recording gets it too. `Bypass rack` mutes the whole chain without
  losing anything; the skyscraper letters dim.
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
| Preview + Program side by side | `frontend/widgets/OBSBasic_StudioMode.cpp` (`SetPreviewProgramMode`, `programWidget`, `programLayout`) | Studio Mode becomes the only mode; zero spacing; labels replaced by the tally tags |
| Transition dropdown + duration | `frontend/widgets/OBSBasic_Transitions.cpp` (`SetTransition`, `ui->transitionDuration`) and `frontend/forms/OBSBasic.ui` | Move the combo + spinbox to a bar on the seam; add unit combo (frames/seconds/ms); frames ↔ ms via `obs_get_video_info().fps_num/fps_den` |
| Take on Space | `frontend/widgets/OBSBasic_Hotkeys.cpp`, `TransitionClicked()` | Default hotkey Space for the studio-mode transition; ignored while a text field has focus |
| Scene thumbnails | new `frontend/components/SceneStrip.{cpp,hpp}`, modelled on `frontend/components/Multiview.cpp` | One `OBSQTDisplay` renders every scene into a row with `obs_source_video_render`, scrolled by an offset; labels as Qt widgets under the display; click → `SetCurrentScene(scene)` (studio: preview), double-click → editor |
| Scene editor | existing sources dock + `OBSBasicPreview` | Opening = select scene for editing, show sources dock + preview in the monitors' area; closing restores the monitors |
| Collapse to one monitor | `OBSBasic_StudioMode.cpp` | Hide `program` or `preview` widget, keep the other at full width |
| Record a scene | new `frontend/utility/SceneRecorder.{cpp,hpp}`; API in `libobs/obs.h` (`obs_canvas_*`) | One `obs_canvas_create(name, &ovi, PROGRAM)` per recording with the scene in channel 0 (`obs_canvas_set_channel`); an `ffmpeg_muxer` output (as `SimpleOutput.cpp` builds `fileOutput`) whose video encoder is bound to `obs_canvas_get_video(canvas)` with `obs_encoder_set_video`, audio encoder to `obs_get_audio()`; `obs_output_set_media(output, canvas video, obs_get_audio())`; `path` = recording folder + scene name |
| Status line | `SceneStrip`, timer at 4 Hz, modelled on `frontend/widgets/OBSBasicStats.cpp` | elapsed = `obs_output_get_total_frames / fps`; free disk = `os_get_free_disk_space(path)` (`platform.h`, same as `OBSBasic::LowDiskSpace`); fps = Δ`video_output_get_total_frames(canvas video)` per second; dropped = `video_output_get_skipped_frames(canvas video)` (encoder lag) + `obs_get_lagged_frames()` (render lag, global) — *not* `obs_output_get_frames_dropped`, which counts network drops |
| FX rack, the chain | `plugins/obs-vst` (`VSTPlugin.cpp`, `vst_filter` with `filter_audio`) | Host each slot with the existing `VSTPlugin` class (load, `effSetSampleRate`/`effSetBlockSize`, `getParameter`/`setParameter`, `effGetChunk`/`effSetChunk` for presets, `EditorWidget` for the UI) but *outside* the filter: a `MasterChain` that runs the slots in order over a float buffer |
| FX rack, where it runs | `libobs/obs-audio.c` `audio_callback()` after the *mix audio* loop, before `discard_audio` | New libobs hook `obs_set_master_audio_processor(cb, param)` called with `mixes[track].data` per track. Live = process track 1 only; Live + Record = also every track the recording output uses (`Settings → Output → Recording → Audio Track`). Bypass = hook installed, chain skipped |
| Rack skyscraper + panel | new `frontend/components/FxRack.{cpp,hpp}` under `programWidget` in `OBSBasic_StudioMode.cpp` | Collapsed: `QToolButton` row of 18 px letters; expanded: eight slot rows, `QComboBox` filled from `obs-vst`'s scan (the `dir_list` in `obs-vst.cpp`) |
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

1. **Build loop** — CI green on the untouched fork. *(this commit)*
2. **Transition seam** — move dropdown + length to the seam, add the unit selector, Space = Take. Small patch; proves the edit→CI loop on real C++.
3. **Scene strip** — the thumbnail component, click → Source, tally frames, scrolling.
4. **Editor + collapse** — double-click opens, close collapses; ⧉ single-monitor mode.
5. **Identity** — window title, About, package name `obs2vmix`, DMG name.
6. **Record a scene** — `SceneRecorder` on the canvas API, red circle per thumbnail, status
   line with time / free disk / fps / dropped. Needs phase 3.
7. **FX rack** — master chain hook in libobs, `obs-vst` host reused outside the filter,
   skyscraper + panel, Live / Live + Record, rack presets.
8. **MIDI learn** — libremidi input, learn mode, mappings in the profile. Needs phase 7.

Phases 6–8 were added 13 Sep 2026 from Marko's spec of the same day; the mockup shows all three.
