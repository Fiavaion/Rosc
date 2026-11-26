// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:rosccam/protocol.dart';

void main() {
  test('messages are read back whole, however the network splits them', () {
    // The same hello the editor's own test reads: 30 characters, 32 bytes of UTF-8.
    final hello = encodeMessage(MessageType.hello, utf8.encode('{ "device": "Pixel 8 — test" }'));
    final picture = encodeMessage(MessageType.picture, [0xFF, 0xD8, 0x00, 0xFF, 0xD9]);
    expect(hello.length, 4 + 1 + 32);
    expect(hello.sublist(0, 5), [33, 0, 0, 0, MessageType.hello]);

    final reader = MessageReader();
    final read = <Message>[];
    for (final byte in [...hello, ...picture]) {
      reader.add([byte]);
      for (var message = reader.next(); message != null; message = reader.next()) {
        read.add(message);
      }
    }
    expect(read.map((m) => m.type), [MessageType.hello, MessageType.picture]);
    expect(read[0].json, {'device': 'Pixel 8 — test'});
    expect(read[1].body, [0xFF, 0xD8, 0x00, 0xFF, 0xD9]);

    // A buffer that has to grow: a picture larger than the reader starts with.
    final big = encodeMessage(MessageType.picture, List.filled(200000, 7));
    reader.add(big.sublist(0, 1000));
    expect(reader.next(), isNull, reason: 'a half message waits');
    reader.add(big.sublist(1000));
    expect(reader.next()?.body.length, 200000);
  });

  test('a message longer than allowed breaks the stream', () {
    final reader = MessageReader()..add([0xFF, 0xFF, 0xFF, 0x7F, 0x10]);
    expect(reader.next(), isNull);
    expect(reader.isBroken, isTrue);
  });

  test('a motion is 40 bytes laid out as the protocol says', () {
    final bytes = const Motion(
      seconds: 12.5,
      rotation: [0.5, 0.5, -0.5, 0.5],
      position: [0.25, -1.5, 1.625],
      positionTracked: true,
    ).toBytes();
    expect(bytes.length, motionBytes);
    final data = ByteData.sublistView(bytes);
    expect(data.getFloat64(0, Endian.little), 12.5);
    expect([for (var i = 0; i < 4; i++) data.getFloat32(8 + 4 * i, Endian.little)], [0.5, 0.5, -0.5, 0.5], reason: 'w, x, y, z');
    expect([for (var i = 0; i < 3; i++) data.getFloat32(24 + 4 * i, Endian.little)], [0.25, -1.5, 1.625]);
    expect(data.getUint32(36, Endian.little), 1, reason: 'the position is tracked');
    expect(ByteData.sublistView(const Motion(seconds: 0, rotation: [1, 0, 0, 0]).toBytes()).getUint32(36, Endian.little), 0);
  });

  test('the state reads as the protocol shows it, and survives what it does not know', () {
    final state = CameraState.fromJson(jsonDecode('''{
      "camera": "A Cam", "support": "Steadicam",
      "operating": true, "live": true, "recording": false, "takeSeconds": 2.5, "hasTake": true,
      "lens": "35 mm", "tStop": "T2.8", "focus": "2.4 m", "aspect": 2.39,
      "marks": ["Anna", "Door"], "warnings": ["Pan peaks at 389.8°/s."], "newer": {"field": 1}
    }''') as Map<String, Object?>);
    expect(state.camera, 'A Cam');
    expect(state.operating && state.live && state.hasTake && !state.recording, isTrue);
    expect(state.takeSeconds, 2.5);
    expect(state.aspect, 2.39);
    expect(state.marks, ['Anna', 'Door']);
    expect(state.warnings.single, startsWith('Pan peaks'));
    expect(CameraState.fromJson({'aspect': 'wide', 'marks': 3}).aspect, 0);
  });

  test("a JPEG's size is read from its header", () {
    final jpeg = Uint8List.fromList([
      0xFF, 0xD8, // start of image
      0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00, // an application segment to step over
      0xFF, 0xC0, 0x00, 0x11, 0x08, 0x02, 0x1C, 0x03, 0xC0, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 540 high, 960 wide
    ]);
    expect(jpegSize(jpeg), (width: 960, height: 540));
    expect(jpegSize(Uint8List.fromList([0x89, 0x50, 0x4E, 0x47])), isNull, reason: 'a PNG is not a JPEG');
  });
}
