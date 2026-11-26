# Third-party parts of RoscCam

RoscCam itself is Apache-2.0, like the rest of this repository. The app is built with, and its
APK contains, the parts below. None of them is copied into this repository: Flutter's packages
come from the Flutter SDK and pub.dev, and the Android libraries from Google's Maven repository,
when the app is built. The app's Licences page (on its first screen) shows the full licence text
of every package Flutter puts in it.

| Part | Licence | Used for |
|---|---|---|
| Flutter (engine and framework) | BSD-3-Clause | The app on Android and iOS |
| material_ui | BSD-3-Clause | Buttons, lists and text fields |
| Dart packages Flutter brings (characters, collection, meta, vector_math and others in `pubspec.lock`) | BSD-3-Clause and similar, as each states | Flutter's own use |
| Kotlin standard library, AndroidX | Apache-2.0 | The Android side |
| ARCore client library (`com.google.ar:core`) | [ARCore Additional Terms of Service](https://developers.google.com/ar/develop/terms) and the [Google APIs Terms of Service](https://developers.google.com/terms) | Tracking where the device is carried, on devices with Google Play Services for AR |

ARCore's terms ask apps to say, on their main screen, that the app runs on Google Play Services
for AR, provided by Google and governed by the Google Privacy Policy, and that its use is subject
to the Google Terms of Service. RoscCam says so on its first screen. ARCore is not open source;
an Android device without Google Play Services for AR runs RoscCam without position tracking.
