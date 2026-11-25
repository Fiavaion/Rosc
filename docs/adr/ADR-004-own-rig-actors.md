# ADR-004: Rigs are Rosc actors on stable engine primitives

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, from research

## Context
Epic ships geometric rigs: the classic rail and crane, and the Beta CineCameraRigs plugin
(`ACineCameraRigRail`). They have no physical limits, and reported bugs include a rail
position that would not keyframe (5.6). Rosc needs speed, acceleration, reach and head-rate
limits, telescoping arms and operator imperfection. Rig class names are saved into levels.

## Decision
Rosc defines its own rig actors (`ARoscRig` and subclasses) built on release-quality
primitives: `USplineComponent` for track, scene components for the kinematic chain, and the
standard `ACineCameraActor` attached to the rig's camera mount. The kinematics and limits are
pure functions in `RoscCore`. Animation is authored as rig-state keys and can be baked to
plain camera transform keys.

## Consequences
- ✅ No dependency on a Beta plugin; rig behaviour is fully testable.
- ✅ Baked shots need no plugin.
- ❌ Epic's per-point spline metadata (focal length and focus on the spline) is not reused;
  Rosc keeps lens and focus on its own Sequencer keys.

## Alternatives considered
1. Extend `ACineCameraRigRail` — rejected: Beta dependency and its known bugs become ours.
2. Control Rig based rigs — rejected: heavier authoring, harder to express speed limits.

## Addendum 2026-11-11: as built in Phase 2
- **One actor class, not subclasses.** `ARoscRig` carries a rig type and per-axis limits
  (`FRoscRigSpec`); the kinematics switch on the type in `RoscCore`. Changing a camera from
  a jib to a crane keeps the same actor, and saved levels reference one class name.
- **Nine axis properties are the saved and keyed surface:** `Travel`, `Traverse`, `Lift`,
  `Swing`, `Boom`, `Extension`, `Pan`, `Tilt`, `Roll`. Sequencer float tracks refer to them by
  name, so they fall under the "never rename a shipped property" boundary. Preset ids are
  saved too (see `docs/rig-presets.md`).
- **Where the keys go.** The designed move is a few cubic keys per axis on the rig's float
  tracks, exact to the solver. Operator movement is one linear key per frame on the camera's
  own transform track, relative to the mount. The two can be rebaked independently.
- **Setters are `CallInEditor`.** Sequencer sets an animated property through `Set<Property>`,
  and the engine drops function calls on actors in an editor world without it
  (LESSON-API-001).
- **Bake to plain camera keys** (no rig, no plugin) is part of the Deliver phase's handoff, not
  of Grip.
