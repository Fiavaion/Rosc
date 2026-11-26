// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// RoscCam: a phone or tablet as the camera of a Rosc shot (ADR-014).
library;

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:material_ui/material_ui.dart';

import 'device.dart';
import 'finder.dart';
import 'link.dart';
import 'protocol.dart';
import 'screens.dart';

/// The app's version, as pubspec.yaml gives it, sent to the editor in the hello.
const String appVersion = '0.1.0';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // Held as a camera: landscape, the top of the device to the left (the protocol's hold).
  await SystemChrome.setPreferredOrientations([DeviceOrientation.landscapeLeft]);
  await SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  runApp(RoscCamApp(controller: RoscCamController()..start()));
}

/// What the app is doing: finding an editor, the link to it, and the motion it sends.
class RoscCamController extends ChangeNotifier {
  RoscCamController({EditorFinder? finder, DeviceMotion? motion})
      : finder = finder ?? EditorFinder(),
        motion = motion ?? DeviceMotion();

  final EditorFinder finder;
  final DeviceMotion motion;
  EditorLink? link;
  bool connecting = false;

  /// Why the last connection failed or ended, to show on the connect screen.
  String problem = '';

  Timer? _sender;
  StreamSubscription<Map<Object?, Object?>>? _hooks;

  void start() {
    finder.start();
    DeviceInfo.listenForBroadcasts(true);
    _hooks = testHooks().listen((hook) {
      if (hook['connect'] is String) {
        connect(hook['connect']! as String);
      } else if (hook['pair'] is String) {
        link?.pair(hook['pair']! as String);
      }
    });
  }

  Future<void> connect(String host, {int port = serverPort}) async {
    if (connecting) {
      return;
    }
    connecting = true;
    problem = '';
    notifyListeners();
    try {
      use(await EditorLink.connect(host.trim(), port: port, deviceName: await DeviceInfo.name(), appVersion: appVersion));
    } on Exception catch (error) {
      problem = 'Could not reach $host: ${error is TimeoutException ? 'no answer' : error.toString().replaceFirst('SocketException: ', '')}. '
          'Check that the RoscCam server is started in the Grip tab and that both are on the same network.';
    }
    connecting = false;
    notifyListeners();
  }

  /// Takes up a link made elsewhere, such as a test's.
  void use(EditorLink newLink) {
    final old = link;
    old?.removeListener(_linkChanged);
    old?.dispose();
    link = newLink..addListener(_linkChanged);
    _linkChanged();
  }

  void disconnect() {
    link?.close();
  }

  void _linkChanged() {
    final current = link;
    if (current == null) {
      return;
    }
    if (current.isPaired) {
      motion.start();
      // Sixty times a second, the protocol's rate, whatever the sensor's own.
      _sender ??= Timer.periodic(const Duration(microseconds: 16667), (_) {
        final now = motion.now();
        if (now != null) {
          current.sendMotion(now);
        }
      });
    } else {
      _sender?.cancel();
      _sender = null;
    }
    if (current.phase == LinkPhase.closed) {
      motion.stop();
      problem = current.problem;
      current.removeListener(_linkChanged);
      link = null;
      // Dispose after the frame: the screen that showed the link may still be listening to it.
      scheduleMicrotask(current.dispose);
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _hooks?.cancel();
    _sender?.cancel();
    link?.dispose();
    finder.dispose();
    motion.stop();
    DeviceInfo.listenForBroadcasts(false);
    super.dispose();
  }
}

class RoscCamApp extends StatelessWidget {
  const RoscCamApp({super.key, required this.controller});
  final RoscCamController controller;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'RoscCam',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF3D8BFF), brightness: Brightness.dark),
        scaffoldBackgroundColor: Colors.black,
      ),
      home: ListenableBuilder(
        listenable: controller,
        builder: (context, _) {
          final link = controller.link;
          if (link == null) {
            return ConnectScreen(controller: controller);
          }
          return ListenableBuilder(
            listenable: link,
            builder: (context, _) => link.isPaired ? CameraScreen(link: link, motion: controller.motion) : PairScreen(link: link),
          );
        },
      ),
    );
  }
}
