# ADR-004: Rigs are UEcine actors on stable engine primitives

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, from research

## Context
Epic ships geometric rigs: the classic rail and crane, and the Beta CineCameraRigs plugin
(`ACineCameraRigRail`). They have no physical limits, and reported bugs include a rail
position that would not keyframe (5.6). UEcine needs speed, acceleration, reach and head-rate
limits, telescoping arms and operator imperfection. Rig class names are saved into levels.

## Decision
UEcine defines its own rig actors (`AUEcineRig` and subclasses) built on release-quality
primitives: `USplineComponent` for track, scene components for the kinematic chain, and the
standard `ACineCameraActor` attached to the rig's camera mount. The kinematics and limits are
pure functions in `UEcineCore`. Animation is authored as rig-state keys and can be baked to
plain camera transform keys.

## Consequences
- ✅ No dependency on a Beta plugin; rig behaviour is fully testable.
- ✅ Baked shots need no plugin.
- ❌ Epic's per-point spline metadata (focal length and focus on the spline) is not reused;
  UEcine keeps lens and focus on its own Sequencer keys.

## Alternatives considered
1. Extend `ACineCameraRigRail` — rejected: Beta dependency and its known bugs become ours.
2. Control Rig based rigs — rejected: heavier authoring, harder to express speed limits.
