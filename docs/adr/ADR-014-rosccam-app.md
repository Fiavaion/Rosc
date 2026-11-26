# ADR-014: RoscCam, the device app, and how it talks to Rosc

**Date:** 2026-11-26
**Status:** Accepted (after Spike #5, 2026-11-26: (b) held, (a) failed on the emulator only)
**Deciders:** The owner (asked for an iOS and Android app called RoscCam; build and test it on the
PC for Android first, then the owner does the iOS work on a Mac)

## Context
Phase 9 (ADR-013) lets a device operate a support through Live Link, and shows the shot on an iPad
or iPhone through Epic's Unreal VCam app and the engine's Virtual Camera plugin. That leaves three
gaps: Android has no Epic app; the device carries none of Rosc's controls; and with the Virtual
Camera plugin on, a render can lose the shot's camera (found at Phase 9's gate). RoscCam is Rosc's
own app for the device, with its own way of talking to the editor.

The app is built on Windows for Android now and on a Mac for iOS later, so it is one codebase that
builds for both. Unreal's WebSocket server plugin is Experimental, which Rosc does not depend on;
the engine's Networking module (TCP listener, UDP sockets) is part of the core runtime.

## Decision
1. **The app is a Flutter app** in `RoscCam/` in this repository, licensed Apache-2.0 like the rest.
   Flutter (BSD-3-Clause) builds Android on Windows and iOS on a Mac from the same Dart code. What
   each platform does differently is kept in two small native files reached through a platform
   channel: the rotation sensor (Android `TYPE_GAME_ROTATION_VECTOR`; iOS Core Motion attitude),
   and, later, AR position (ARCore; ARKit). The Flutter SDK is a build tool, not shipped in the
   plugin.
2. **Rosc runs its own server**, in RoscEditor, on the engine's Networking module. It is off until
   the user starts it in the Grip tab, and stops when the editor closes.
   - While it runs, it announces itself once a second by UDP broadcast on port 47820, so the app
     can list the editors on the network. The app can also be given an address.
   - One TCP connection on port 47821 carries everything, as messages of a 4-byte length, a 1-byte
     type and a body: hello, pairing, motion (60 a second, binary), controls and state (JSON), and
     picture frames (JPEG).
3. **Pairing is the consent gate for the picture.** A device that connects is shown a request in
   the Grip tab with a four-digit code; the user types the code on the device. Until a device is
   paired, it gets no frame and no state, and its motion is ignored. A pairing lasts until the
   server stops or the user unpairs. Frames go only to the paired device on the local network.
4. **Motion comes in as Live Link.** The server is a Live Link source of Rosc's own, with one
   Transform subject per paired device, so Operate, Zero, Record and Bake (ADR-013) work on it
   unchanged.
5. **The picture is a scene capture of the camera**, 960 x 540 at 24 frames a second by default,
   read back without stalling the editor and coded as JPEG off the game thread. It shows what the
   camera sees, without the monitor tools; the app draws frame lines for the delivery aspect
   itself, from the state it is sent.
6. **Controls are the panel's own functions.** Each control from the device calls what the same
   control in the panel calls, so it is one undo step and gives the same findings.

*Spike #5 decision rule (written before the spike runs):* on the Android emulator on this PC,
(a) a Flutter app reads the device's rotation through the platform channel, and the emulator's
rotation can be set from a script; and (b) the app shows 960 x 540 JPEG frames sent over TCP from
the PC at 20 frames a second or more. If both hold, keep the decision. If (a) fails on the
emulator only, keep the decision and give debug builds a way for the test script to set the
rotation the app sends; the sensor is then a hand check on a real phone. If (b) fails, lower the
default picture to 640 x 360 and measure again; if that also fails, Flutter is dropped for a
native Android app (Kotlin), and the iOS app becomes a second codebase.

## Spike #5 result (2026-11-26)
Run on the PC's Android emulator (API 36, software graphics) with Flutter 3.50.1. A script on the
PC sent 960 x 540 JPEG frames of about 44 KB at 30 a second over TCP.
- (b) held: the app received and showed 30 frames a second, every second after the first.
- (a) failed on the emulator only: the game rotation vector reaches the app through the platform
  channel, but the emulator's console cannot turn the device. Gyroscope values set from the
  console are not applied (they read back as zero), and the app's yaw stayed where it was.
So the decision is kept. Debug builds of the app take a rotation from the test script (an Android
broadcast) in place of the sensor's, and the sensor itself is a hand check on a real phone.

## Consequences
- RoscCam does not need the Virtual Camera plugin, so it avoids the render problem in ADR-013.
- The editor listens on the local network while the server runs. Windows asks once whether to let
  the editor through its firewall; the guide says to allow it on private networks only.
- JPEG frames cost more bandwidth than a video codec (about 3 to 6 MB/s at the default). That is
  fine on a local network and keeps both ends simple.
- The protocol is Rosc's own. A change to it raises its version in the hello message; the app
  says when the editor's Rosc is older or newer than it understands.
- Store listings need paid developer accounts: the owner's decision. Until then the Android app
  installs from its APK, and the iOS app from Xcode.

## Alternatives considered
1. **Pixel Streaming for the picture.** Rejected: it comes with the Virtual Camera plugin's
   render problem, and needs a WebRTC stack in the app.
2. **Remote Control API for the controls.** Rejected: it is a second server to set up, and its
   calls are by object path, which would tie the app to Rosc's internals.
3. **A native app per platform.** Rejected for now: two codebases for one app. Kept as the
   fallback in Spike #5's rule.
4. **React Native.** Rejected: showing a stream of frames fast needs native code on both platforms
   anyway, and Flutter's image pipeline does it in Dart.
