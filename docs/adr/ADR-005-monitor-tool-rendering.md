# ADR-005: How the monitor tools are drawn

**Date:** 2026-11-11
**Status:** Proposed — pending Spike #1 (run at the start of Phase 3)
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

## Consequences
Recorded when the spike completes and this ADR is accepted.
