// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// The RoscCam protocol, version 1, as docs/rosccam-protocol.md in this repository describes it.
library;

import 'dart:convert';
import 'dart:typed_data';

const int protocolVersion = 1;
const int discoveryPort = 47820;
const int serverPort = 47821;
const int maxMessageBytes = 8 * 1024 * 1024;
const int motionBytes = 40;

/// The type byte of each message.
abstract final class MessageType {
  static const int hello = 0x01;
  static const int welcome = 0x02;
  static const int pair = 0x03;
  static const int paired = 0x04;
  static const int picture = 0x10;
  static const int motion = 0x20;
  static const int control = 0x30;
  static const int state = 0x31;
  static const int result = 0x32;
}

/// A message: its length, its type and its body.
Uint8List encodeMessage(int type, List<int> body) {
  final bytes = Uint8List(5 + body.length);
  ByteData.sublistView(bytes).setUint32(0, body.length + 1, Endian.little);
  bytes[4] = type;
  bytes.setRange(5, bytes.length, body);
  return bytes;
}

Uint8List encodeJson(int type, Map<String, Object?> json) => encodeMessage(type, utf8.encode(jsonEncode(json)));

class Message {
  const Message(this.type, this.body);
  final int type;
  final Uint8List body;

  /// The body read as JSON, or null when it is not a JSON object.
  Map<String, Object?>? get json {
    try {
      final value = jsonDecode(utf8.decode(body));
      return value is Map<String, Object?> ? value : null;
    } on FormatException {
      return null;
    }
  }
}

/// Reads whole messages out of a stream of bytes, however the network splits it.
class MessageReader {
  Uint8List _buffer = Uint8List(64 * 1024);
  int _start = 0;
  int _end = 0;
  bool _broken = false;

  /// True once a message too long or of no length has come: the connection is to be closed.
  bool get isBroken => _broken;

  void add(List<int> bytes) {
    if (_end + bytes.length > _buffer.length) {
      final pending = _end - _start;
      final needed = pending + bytes.length;
      final next = needed > _buffer.length ? Uint8List(needed * 2) : _buffer;
      next.setRange(0, pending, _buffer, _start);
      _buffer = next;
      _start = 0;
      _end = pending;
    }
    _buffer.setRange(_end, _end + bytes.length, bytes);
    _end += bytes.length;
  }

  Message? next() {
    if (_broken || _end - _start < 4) {
      return null;
    }
    final length = ByteData.sublistView(_buffer, _start, _start + 4).getUint32(0, Endian.little);
    if (length == 0 || length > maxMessageBytes) {
      _broken = true;
      return null;
    }
    if (_end - _start < 4 + length) {
      return null;
    }
    final type = _buffer[_start + 4];
    final body = Uint8List.fromList(Uint8List.sublistView(_buffer, _start + 5, _start + 4 + length));
    _start += 4 + length;
    if (_start == _end) {
      _start = 0;
      _end = 0;
    }
    return Message(type, body);
  }
}

/// The device's motion: its rotation (w, x, y, z) in its world frame, right-handed and z up, and
/// where it is in metres when the platform tracks that.
class Motion {
  const Motion({
    required this.seconds,
    required this.rotation,
    this.position = const [0, 0, 0],
    this.positionTracked = false,
  });

  final double seconds;
  final List<double> rotation;
  final List<double> position;
  final bool positionTracked;

  Uint8List toBytes() {
    final data = ByteData(motionBytes);
    data.setFloat64(0, seconds, Endian.little);
    for (var i = 0; i < 4; i++) {
      data.setFloat32(8 + 4 * i, rotation[i], Endian.little);
    }
    for (var i = 0; i < 3; i++) {
      data.setFloat32(24 + 4 * i, position[i], Endian.little);
    }
    data.setUint32(36, positionTracked ? 1 : 0, Endian.little);
    return data.buffer.asUint8List();
  }
}

/// What the editor says about its camera (the State message).
class CameraState {
  const CameraState({
    this.camera = '',
    this.support = '',
    this.operating = false,
    this.live = false,
    this.recording = false,
    this.takeSeconds = 0,
    this.hasTake = false,
    this.lens = '',
    this.tStop = '',
    this.focus = '',
    this.aspect = 0,
    this.marks = const [],
    this.warnings = const [],
  });

  factory CameraState.fromJson(Map<String, Object?> json) {
    String text(String key) => json[key] is String ? json[key]! as String : '';
    bool flag(String key) => json[key] == true;
    double number(String key) => json[key] is num ? (json[key]! as num).toDouble() : 0;
    List<String> lines(String key) => json[key] is List ? (json[key]! as List).whereType<String>().toList() : const [];
    return CameraState(
      camera: text('camera'),
      support: text('support'),
      operating: flag('operating'),
      live: flag('live'),
      recording: flag('recording'),
      takeSeconds: number('takeSeconds'),
      hasTake: flag('hasTake'),
      lens: text('lens'),
      tStop: text('tStop'),
      focus: text('focus'),
      aspect: number('aspect'),
      marks: lines('marks'),
      warnings: lines('warnings'),
    );
  }

  final String camera;
  final String support;
  final bool operating;
  final bool live;
  final bool recording;
  final double takeSeconds;
  final bool hasTake;
  final String lens;
  final String tStop;
  final String focus;

  /// The delivery aspect to draw frame lines for; zero for the whole picture.
  final double aspect;
  final List<String> marks;
  final List<String> warnings;
}

/// The width and height a JPEG says it is, or null when the bytes are not a JPEG.
({int width, int height})? jpegSize(Uint8List bytes) {
  if (bytes.length < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
    return null;
  }
  var at = 2;
  while (at + 9 < bytes.length) {
    if (bytes[at] != 0xFF) {
      return null;
    }
    final marker = bytes[at + 1];
    final length = (bytes[at + 2] << 8) | bytes[at + 3];
    // The start-of-frame markers, baseline to lossless, carry the size.
    final isFrame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    if (isFrame) {
      final height = (bytes[at + 5] << 8) | bytes[at + 6];
      final width = (bytes[at + 7] << 8) | bytes[at + 8];
      return (width: width, height: height);
    }
    at += 2 + length;
  }
  return null;
}
