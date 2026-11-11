# ADR-001: Module layout and engine-version isolation

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** user (engine baseline, commercial distribution), Claude (layout)

## Context
The plugin targets UE 5.8 and must follow later engine versions, including the API churn
expected at UE6. Fab compiles the plugin separately for each engine version. Module names and
class names are baked into user assets, so the layout is hard to change after release.

## Decision
- One plugin, `UEcine`, with modules split by dependency weight:
  - `UEcineCore` (Runtime): maths and data only (optics, exposure, depth of field, move solver,
    rig limits, camera and lens database). Depends on `Core`, `CoreUObject`, `Json`.
  - `UEcineRuntime` (Runtime): actors and components that apply Core results to the engine.
  - `UEcineEditor` (Editor): Slate panel, viewport overlay, Sequencer integration.
  - `UEcineRender` (Editor, Phase 5) and `UEcineAI` (Editor, Phase 6) are added when their
    phases start, so no empty modules exist before then.
- All engine-version checks live in `UEcineCompat.h`. No other file tests the engine version.
- The camera state lives on a component (`UUEcineCameraPackageComponent`) attached to a
  standard `ACineCameraActor`, writing standard properties, so the camera survives without
  the plugin.

## Consequences
- ✅ Core logic is testable without a world and ports cleanly across engine versions.
- ✅ A project opened without the plugin keeps its cameras and baked animation.
- ❌ More modules to maintain than a single-module plugin.

## Alternatives considered
1. Single module — rejected: maths would pick up editor dependencies and become hard to test.
2. Subclassing `ACineCameraActor` — rejected: the actor would disappear from levels when the
   plugin is absent.
