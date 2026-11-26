# ADR-013: Operating the camera from a device

**Date:** 2026-11-26
**Status:** Accepted, except point 3, which is Proposed pending Spike #4
**Deciders:** The owner (asked for the support's head to be driven from the viewport and from the
VCam app on a device, with stabilise and filter options for the recorded motion)

## Context
Rosc's supports are posed by numbers and by blocked moves (ADR-004). While looking through a
camera on a support, the camera is held on its mount, so the view cannot be turned. Operators
want to frame by hand: in the viewport, and with a phone or tablet held like a camera.

In Unreal Engine 5.8, Epic's free Unreal VCam app (iPhone and iPad only) streams the editor's
picture to the device through the Virtual Camera plugin's Pixel Streaming output
(`UVCamPixelStreamingSession`). It sends the device's motion back as a Live Link subject with the
Transform role. A VCam component moves its camera only through modifiers; with none, it only
streams. Live Link is a release plugin. Virtual Camera is Beta and off by default, and it brings
Pixel Streaming and about twenty other plugins with it.

## Decision
1. **Head from the viewport.** While the user looks through a camera on a support, a turn of the
   view becomes the head's pan and tilt, kept within the head's limits. The camera stays on its
   mount, and each look is one undo step.
2. **Motion comes in through Live Link.** Rosc depends on the Live Link plugin and reads any
   subject that carries a transform (the Transform role, or a role built on it, such as Camera).
   The Unreal VCam app is one such source; trackers that Live Link already supports are others.
   Operate takes the way the device is held as the start. From there, the device's turn drives
   pan, tilt and roll. On a Steadicam, handheld rig or drone, its movement drives travel, traverse
   and lift, in the rig's own frame. The limits are reported, not enforced.
3. **The picture goes out through the engine's VCam** (Proposed, pending Spike #4). Show on Device
   adds a VCam component to the camera with a Pixel Streaming output and no modifiers, made by
   class path, so Rosc does not link the Virtual Camera plugin and does not load it for users who
   never use it. If the plugin is off, Rosc asks to turn it on for the project, and the editor
   restarts.
   *Spike #4 decision rule:* in a headless editor with the plugin on, the component and its output
   can be made by class path on a Rosc camera and activated, and the output's target is that
   camera. If so, keep the decision. If not, Show on Device is dropped, and the user guide gives
   the VCam setup steps instead.
4. **Takes.** Record plays the open shot and samples the device's drive of the rig once per frame
   of the sequence. The take is kept until it is baked or replaced.
5. **Processing is pure maths in RoscCore.** Each axis of the take is unwrapped, so a pan across
   ±180° does not jump. It is then smoothed by a Gaussian over time, with mirrored ends so the
   first and last frames hold. Smoothing (Off, Light, Medium, Heavy) and Stabilise (0 to 100 %)
   set the Gaussian's width together. A Gaussian is symmetric, so it adds no lag, and it does not
   overshoot, so a whip pan does not swing past its end. A locked axis holds its value from the
   first frame. Findings for speed and acceleration come from the processed take, measured
   against the rig's limits.
6. **Bake** writes one key per frame on the rig's axes into the open shot, in one undo step, as the
   focus racks do.

## Consequences
- Rosc gains one plugin dependency, Live Link, which is light and release-grade.
- The VCam picture depends on a Beta engine plugin, behind a choice the user makes, and is never
  linked. If its class paths change in a later engine version, the change goes in `RoscCompat.h`.
- Android devices have no Epic app. Their motion still comes in through any Live Link source, and
  a Rosc app of its own is Phase 10 (RoscCam).
- A take that is not baked is lost when the editor closes.

## Alternatives considered
1. **Use VCam's own modifiers to move the camera.** Rejected: the camera would leave its support,
   and nothing would check the move against the gear.
2. **Record with Take Recorder.** Rejected: it records the result, not the device's motion, so
   there would be nothing raw left to process again.
3. **A Butterworth filter.** Rejected: it rolls off more sharply, but it overshoots on a step.
4. **Link the Virtual Camera plugin.** Rejected: every Rosc user would load Pixel Streaming.
