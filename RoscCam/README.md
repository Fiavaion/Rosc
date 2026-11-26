# RoscCam

RoscCam makes a phone or tablet the camera of a Rosc shot. It shows the camera's picture, turns the
camera's support as you turn the device, and carries the controls for a take: Operate, Zero, Record,
Bake Take, the next and previous lens of the kit, and a rack to a focus mark. How it talks to the
editor is in [docs/rosccam-protocol.md](../docs/rosccam-protocol.md); why it is built this way is in
[ADR-014](../docs/adr/ADR-014-rosccam-app.md). How to use it is in the Rosc user guide.

It is a Flutter app: one codebase for Android and iOS. Android is built and tested; iOS is to come.

## Build for Android (Windows)

You need [Flutter](https://docs.flutter.dev/get-started/install) (on `PATH`, or `FLUTTER_ROOT` set to
its folder) and the Android SDK (`ANDROID_HOME` set to its folder). From the repository's root:

```powershell
powershell -File tools/build-rosccam.ps1            # checks, tests and builds the APK
powershell -File tools/build-rosccam.ps1 -Debug     # a debug build, with the test hooks
```

The APK is in `RoscCam/build/app/outputs/flutter-apk/`. Install it on a phone with
`adb install app-release.apk`, or copy it to the phone and open it there (allow installing from
that source when Android asks).

A release build is signed with the key that `android/key.properties` names (`storeFile`,
`storePassword`, `keyAlias`, `keyPassword`; the file and the key are never committed). Without
that file it is signed with the computer's Android debug key. Android installs an update only
over an app signed with the same key, so moving to another key means uninstalling the app first.

## Test

- `flutter test` in this folder runs the app's own tests (the protocol, the link, the screens).
- `powershell -File tools/test-rosccam.ps1` runs the app on the Android emulator against a headless
  editor: it pairs, turns the device 30° and checks that the support pans 30°, and checks that the
  device's screen shows the camera's picture. A debug build takes the turn from the script by an
  Android broadcast, because the emulator's own rotation cannot be turned from a script
  (`MainActivity.kt` lists the broadcasts). A release build has no such hooks.

## Code

| File | What it does |
|---|---|
| `lib/protocol.dart` | The messages, as the protocol document lays them out |
| `lib/link.dart` | The connection to an editor: hello, pairing, then motion and controls out, picture and state in |
| `lib/finder.dart` | Hears the editors' announcements on the network |
| `lib/device.dart` | What the device's own code gives: rotation, AR position, name |
| `lib/screens.dart` | Find an editor, pair, and the camera screen |
| `android/.../MainActivity.kt` | Android's side of `lib/device.dart` |

## iOS (to come)

The iOS side of `lib/device.dart` goes in `ios/Runner/AppDelegate.swift`, on the same channels:

- `rosccam/rotation` (event channel): Core Motion's attitude in the `xArbitraryZVertical` frame,
  as `[w, x, y, z]`, at 60 a second or more.
- `rosccam/position` (event channel): ARKit's position as `[x, y, z, tracked]` in metres, in the
  same frame as the rotation (z up), with `tracked` 1 while ARKit tracks.
- `rosccam/device` (method channel): `name` gives the device's name.
- `Info.plist` needs `NSLocalNetworkUsageDescription` and `NSCameraUsageDescription` (ARKit), and
  landscape-left as the only orientation; hearing the editors' broadcasts needs Apple's multicast
  networking entitlement. Without it, the editor's address is typed in.
