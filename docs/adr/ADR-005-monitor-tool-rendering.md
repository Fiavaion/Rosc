# ADR-005: How the monitor tools are drawn

**Date:** 2026-11-11
**Status:** Accepted (candidate A), 2026-11-11, after Spike #1
**Deciders:** Claude

## Context
False colour, zebras and focus peaking are per-pixel effects on the viewport image. Waveform
and histogram need the image's luminance distribution. They must not appear in final renders,
must not need the user to edit their post-process volumes, and must stay interactive.

## Candidates
A. A scene view extension (`FSceneViewExtensionBase`) that adds a render-graph pass to editor
   viewports only.
B. Post-process materials added to the viewed camera as blendables, removed for rendering.
C. Reading the viewport back to the CPU and drawing everything in Slate.

## Decision rule (written before measuring)
Build false colour and a waveform each way that compiles in under a day, on the sample scene
at 1920×1080 on the RTX 5070 Ti. Choose the first candidate in the order A, B, C that meets
all of:
1. viewport frame time rises by no more than 1.5 ms with false colour and waveform both on;
2. the effect never appears in a Movie Render Graph output with the tools left switched on;
3. it works in the level viewport when piloting a camera and in the Sequencer cinematic
   viewport;
4. it needs no change to user assets or post-process volumes.
If A fails only on criterion 3, use B for per-pixel effects. The waveform may use C if its
update rate reaches 15 Hz within the 1.5 ms budget.

## Spike #1 result (2026-11-11)
Candidate A was built first and met all four criteria, so B and C were not built.

| Criterion | Result | How it was checked |
|---|---|---|
| 1. No more than 1.5 ms | False colour and waveform together add 0.14 to 0.26 ms to a 1920×1080 frame; all five tools together add 0.16 to 0.26 ms (median of 100 frames, three runs, RTX 5070 Ti, waiting for the graphics card after every frame) | `Rosc.Monitor.Cost`, which also fails if the cost goes over the budget |
| 2. Never in a render | The pass is active only for views drawn through a level editor viewport. A scene capture taken with every tool on is the same picture as with the tools off, and a monitor nobody has activated draws nowhere | `Rosc.Editor.Monitor.Viewport`, `Rosc.Monitor.Picture`. A Movie Render Graph render with the tools left on is added to the Phase 5 acceptance checks |
| 3. Level and cinematic viewports | Drawn in the real level viewport of a headless editor: the whole picture turns to bands and greys, and the scope panel appears | `Rosc.Editor.Monitor.Viewport`. Piloting a camera and the Sequencer cinematic viewport use the same viewport class; both are on the Phase 3 hand-check list |
| 4. No change to user assets | Nothing is added to cameras, volumes or materials; the tools are per-user editor preferences | by construction |

## Decision
Candidate A. `FRoscMonitorViewExtension` (module `RoscMonitor`) adds one full-screen pass
after tone mapping that applies false colour, zebras and peaking and draws the scopes, plus one
small compute pass that counts signal levels for the waveform and histogram when a scope is on.
`URoscMonitorSubsystem` (module `RoscEditor`) owns which tools are on and names the views
the pass may draw in.

## Consequences
- ✅ The tools measure the picture as displayed, after tone mapping, which is what an on-set
  monitor measures. When a view anti-aliases after tone mapping (FXAA, SMAA) the pass runs
  after that instead, so the tools' own graphics stay sharp.
- ✅ Scopes cost almost nothing: every pixel of a picture up to 960 wide is counted on the
  graphics card (every second pixel at 1920), with no read-back to the CPU.
- ✅ Tools are view state: not part of undo history, saved per user, never in a render.
- ❌ The false colour bands are set against the engine's default tone curve (the figures are
  in `RoscMonitorTypes.cpp` and re-measured by `Rosc.Monitor.Picture`). A project that
  replaces the tone curve or adds a display transform moves where grey sits, and the bands do
  not follow. An engine update that changes the tone curve fails that test, which is the
  prompt to re-measure.
- ❌ The pass needs Shader Model 5 and a plugin shader directory, so `RoscMonitor` loads
  before the engine compiles its global shaders (`PostConfigInit`).
- ✅ Lines and words (frame lines, safe areas, centre mark, data readout, scope titles, the
  false colour legend) are drawn by the same pass with the engine's canvas
  (`AddDrawCanvasPass`), straight after the picture tools. They are therefore in the picture
  the viewport shows and a test reads back, in game view and while piloting, and are kept out
  of renders by the same rule as the rest. An editor overlay (debug canvas or Slate) was
  rejected: it is not in the pixels read back, so it could not be checked headlessly.
- Frame lines and the data readout belong to a camera, not to the view settings. The pass is
  told per view (keyed by what the view renders to) which camera it looks through;
  `URoscMonitorSubsystem` works that out every editor tick from each level viewport's
  locked camera, so piloting and Sequencer camera cuts both count.
- ❌ The words use the engine's small stats font, scaled with the viewport's DPI scale. Its
  size is not ours to choose; whether it reads well on a 4K display is a hand check.
- Not handled: HDR display output. The signal scale assumes a standard-range display.
