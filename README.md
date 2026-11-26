# Rosc

A camera department inside the Unreal Editor, for directors and DOPs. You choose a real camera
body, recording format and lens kit. You put the camera on a dolly, a crane or a Steadicam and
block a move. You check exposure and focus with monitor tools, then render a review file and a
VFX handoff. All of it happens in one panel, in film language: T-stops, shutter angle, ND,
sensor formats, marks and racks.

Rosc (said "rusk") is the Irish word for "eye". It is free and open source under the
[Apache License 2.0](LICENSE).

Status: beta, version 0.1.0. Built and tested on Unreal Engine 5.8.3 on Windows.

## What it does

| Tab | What you can do |
|---|---|
| Camera | Pick a camera body and recording format, a lens kit and a lens; set ISO, shutter angle, ND and T-stop; choose frame lines and a delivery aspect |
| Grip | Put the camera on a tripod, slider, dolly, jib, telescoping crane, Steadicam, handheld or drone; block a move between marks; see when a move goes past what the gear can do; add operator movement; bake it to Sequencer |
| Focus | Click a subject to focus on it; keep focus on a point as things move; store marks and rack between them; read near, far and hyperfocal distances |
| Monitor | Frame lines, safe areas, false colour, zebras, waveform, histogram, focus peaking and a camera data readout in the viewport |
| Shots | A shot list tied to Sequencer shots, storyboard frames, an overhead plan, and a shot list and camera report to export |
| Deliver | Draft, Review, VFX Passes, Master, YouTube and DCP Source renders through the Movie Render Graph, with a preflight check that catches common mistakes |
| AI (optional) | A depth map and a matte of a still or plate, a lens estimate from a picture, and a shot assistant that turns a description into an undoable camera move |

What Rosc makes is ordinary Unreal data: Cine Camera properties, Sequencer keys and Movie
Render Graph settings. Use "Bake for handoff" and a project opens without the plugin.

The user guide is `HostProject/Plugins/Rosc/Docs/UserGuide.html`. Open it in a browser, or
press the **?** button in the panel.

## Install

Rosc is source for now; there is no ready-built download yet.

1. Copy `HostProject/Plugins/Rosc` into your project's `Plugins` folder.
2. Open the project. The editor offers to build the plugin. This needs a C++ toolchain set up
   for Unreal Engine 5.8.
3. Open the panel from **Window > Cinematics > Rosc**.

The sample stage, a shallow-focus shot to try things on, is in the plugin's content at
`/Rosc/Sample/RoscSampleStage`.

## AI features

The AI tab is optional. Nothing in it runs until you choose to use it.

- Models are not shipped with Rosc. Each one is downloaded from its makers' page when you press
  Download, under its makers' licence. The models offered are under MIT or Apache-2.0.
- The shot assistant uses the Claude API with your own key, billed to you. Nothing leaves your
  machine until you have entered a key and agreed in the AI tab.

[`THIRD_PARTY_NOTICES.md`](HostProject/Plugins/Rosc/THIRD_PARTY_NOTICES.md) lists each model,
package and service and the terms each one comes under.

## Building and testing

The repository holds a small host project (`HostProject/RoscHost.uproject`) that builds and
tests the plugin. The scripts read the engine's folder from `UE_ROOT` (default
`D:\Program Files\Epic Games\UE_5.8`).

```powershell
powershell -File tools/build.ps1                                # build the editor and the plugin
powershell -File tools/test.ps1                                 # all Rosc tests, headless
powershell -File tools/test.ps1 -Filter Rosc.Core.Optics        # some of them
powershell -File tools/test.ps1 -Rendering                      # include the tests that read pixels
powershell -File tools/package.ps1                              # package the plugin as Fab would
```

The design decisions are recorded in [`docs/adr/`](docs/adr/).

## Licence and names

Rosc is copyright 2026 Fiavaion, under the [Apache License 2.0](LICENSE). The licence gives no
rights to the name Rosc.

Camera, lens and equipment names in the database are trademarks of their owners. They are used
only to say which equipment a figure describes. Rosc is not made, endorsed or sponsored by any
camera or lens maker, or by Epic Games.
