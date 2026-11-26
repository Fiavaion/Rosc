// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// What the phone or tablet itself gives the app, through the platform's own code: its rotation,
/// its position where AR tracks it, its name, and (in debug builds only) the test script's hooks.
library;

import 'dart:async';

import 'package:flutter/services.dart';

import 'protocol.dart';

class DeviceMotion {
  static const _rotationChannel = EventChannel('rosccam/rotation');
  static const _positionChannel = EventChannel('rosccam/position');

  StreamSubscription<Object?>? _rotation;
  StreamSubscription<Object?>? _position;
  List<double>? _latestRotation;
  List<double> _latestPosition = const [0, 0, 0];
  bool _positionTracked = false;

  /// Why there is no motion, to show the user; empty while there is.
  String problem = '';

  bool get hasRotation => _latestRotation != null;
  bool get isPositionTracked => _positionTracked;

  void start() {
    _rotation ??= _rotationChannel.receiveBroadcastStream().listen((value) {
      if (value is List && value.length == 4) {
        _latestRotation = value.map((v) => (v as num).toDouble()).toList();
      }
    }, onError: (Object error) {
      problem = error is PlatformException ? (error.message ?? 'No rotation sensor.') : 'No rotation sensor.';
    });
    // Position comes only on a device that AR tracks; without it, the rotation is all there is.
    _position ??= _positionChannel.receiveBroadcastStream().listen((value) {
      if (value is List && value.length == 4) {
        _latestPosition = value.take(3).map((v) => (v as num).toDouble()).toList();
        _positionTracked = value[3] == 1;
      }
    }, onError: (Object _) => _positionTracked = false);
  }

  void stop() {
    _rotation?.cancel();
    _position?.cancel();
    _rotation = null;
    _position = null;
    _positionTracked = false;
  }

  /// The motion now, or null before the sensor has said anything.
  Motion? now() {
    final rotation = _latestRotation;
    if (rotation == null) {
      return null;
    }
    return Motion(
      seconds: DateTime.now().microsecondsSinceEpoch / 1e6,
      rotation: rotation,
      position: _latestPosition,
      positionTracked: _positionTracked,
    );
  }
}

abstract final class DeviceInfo {
  static const _channel = MethodChannel('rosccam/device');

  /// The device's own name, such as "Google Pixel 8", for the editor to show.
  static Future<String> name() async {
    try {
      return await _channel.invokeMethod<String>('name') ?? 'Device';
    } on MissingPluginException {
      return 'Device';
    }
  }

  /// Lets the app hear the editors' announcements: Wi-Fi drops broadcasts to save power otherwise.
  static Future<void> listenForBroadcasts(bool on) async {
    try {
      await _channel.invokeMethod<void>('broadcasts', on);
    } on MissingPluginException {
      // Not on this platform: nothing to turn on.
    }
  }
}

/// What a test script sends a debug build of the app: an editor to connect to and a code to pair
/// with. A release build has no hooks, and this stream stays empty.
Stream<Map<Object?, Object?>> testHooks() => const EventChannel('rosccam/test')
    .receiveBroadcastStream()
    .where((event) => event is Map)
    .cast<Map<Object?, Object?>>()
    .handleError((Object _) {});
