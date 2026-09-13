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
├──────────────────────────────────┴───────────────────────────────────┤
│ Scenes                                       1–9 preview · space take│
│ [ thumb ] [ thumb ] [ thumb ] [ thumb ] [ thumb ]  ‹ scroll for more ›│
│  Intro     Cam 1     Cam 2     Slides    2-shot                      │
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
| Registration of new files | `frontend/cmake/ui-components.cmake` | Add SceneStrip sources |
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
