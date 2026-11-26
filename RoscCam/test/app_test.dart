// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:rosccam/link.dart';
import 'package:rosccam/main.dart';
import 'package:rosccam/protocol.dart';
import 'package:rosccam/screens.dart';

/// An editor at the other end of a link, as far as the app can tell.
class FakeEditor {
  final StreamController<List<int>> incoming = StreamController();
  final MessageReader _reader = MessageReader();
  final List<Message> sent = [];

  EditorLink link() => EditorLink(incoming.stream, _receive, deviceName: 'Test phone', appVersion: '0.1.0');

  void _receive(List<int> bytes) {
    _reader.add(bytes);
    for (var message = _reader.next(); message != null; message = _reader.next()) {
      sent.add(message);
    }
  }

  void say(int type, Map<String, Object?> json) => incoming.add(encodeJson(type, json));

  /// The JSON of the controls the app has sent.
  List<Map<String, Object?>?> get controls => sent.where((m) => m.type == MessageType.control).map((m) => m.json).toList();
}

void main() {
  test('the link says hello, pairs, and only then sends motion and controls', () async {
    final editor = FakeEditor();
    final link = editor.link();
    expect(editor.sent.single.type, MessageType.hello);
    expect(editor.sent.single.json, {'protocol': 1, 'device': 'Test phone', 'app': '0.1.0'});

    editor.say(MessageType.welcome, {'protocol': 1, 'editor': 'Rosc 0.1.0'});
    await pumpEventQueue();
    expect(link.editor, 'Rosc 0.1.0');
    expect(link.phase, LinkPhase.pairing);

    link.control('operate');
    link.sendMotion(const Motion(seconds: 0, rotation: [1, 0, 0, 0]));
    expect(editor.sent.length, 1, reason: 'nothing but the hello goes before pairing');

    link.pair('1234');
    expect(editor.sent.last.json, {'code': '1234'});
    editor.say(MessageType.paired, {'paired': false, 'reason': 'That is not the code the editor shows.'});
    await pumpEventQueue();
    expect(link.isPaired, isFalse);
    expect(link.problem, startsWith('That is not the code'));

    editor.say(MessageType.paired, {'paired': true});
    editor.say(MessageType.state, {'camera': 'A Cam', 'operating': false, 'marks': ['Mark 1: 2 m']});
    await pumpEventQueue();
    expect(link.isPaired, isTrue);
    expect(link.state.camera, 'A Cam');

    link.control('lens', {'step': 1});
    expect(editor.controls.single, {'action': 'lens', 'step': 1});
    link.sendMotion(const Motion(seconds: 1, rotation: [1, 0, 0, 0]));
    expect(editor.sent.last.type, MessageType.motion);
    expect(editor.sent.last.body.length, motionBytes);

    editor.say(MessageType.result, {'action': 'lens', 'ok': false, 'message': 'This is the last lens of the kit.'});
    await pumpEventQueue();
    expect(link.result.value?.ok, isFalse);
    expect(link.result.value?.message, 'This is the last lens of the kit.');

    await editor.incoming.close();
    await pumpEventQueue();
    expect(link.phase, LinkPhase.closed);
    expect(link.problem, contains('ended the connection'));
    link.dispose();
  });

  test('an editor of another protocol is let go, saying which to update', () async {
    final editor = FakeEditor();
    final link = editor.link();
    editor.say(MessageType.welcome, {'protocol': 2, 'editor': 'Rosc 0.3.0'});
    await pumpEventQueue();
    expect(link.phase, LinkPhase.closed);
    expect(link.problem, contains('Update the older'));
    link.dispose();
  });

  test('frame lines are round the delivered part of the picture', () {
    const picture = Size(960, 540);
    expect(FrameLines.frameIn(picture, 0), Offset.zero & picture, reason: 'no delivery aspect: the whole picture');
    final scope = FrameLines.frameIn(picture, 2.39);
    expect(scope.width, 960);
    expect(scope.height, closeTo(960 / 2.39, 1e-9));
    expect(scope.center, picture.center(Offset.zero));
    final square = FrameLines.frameIn(picture, 1);
    expect(square.size, const Size(540, 540));
  });

  testWidgets('the operator pairs, then works the camera from the controls', (tester) async {
    await tester.binding.setSurfaceSize(const Size(915, 412));
    final editor = FakeEditor();
    final controller = RoscCamController();
    await tester.pumpWidget(RoscCamApp(controller: controller));
    expect(find.text('Editors on this network'), findsOneWidget);

    controller.use(editor.link());
    editor.say(MessageType.welcome, {'protocol': 1, 'editor': 'Rosc 0.1.0'});
    await tester.pumpAndSettle();
    expect(find.text('Pair with Rosc 0.1.0'), findsOneWidget);
    await tester.enterText(find.byType(TextField), '48a21x9');
    expect(find.text('4821'), findsOneWidget, reason: 'the code takes four digits and nothing else');
    await tester.tap(find.text('Pair'));
    expect(editor.sent.last.json, {'code': '4821'});

    editor.say(MessageType.paired, {'paired': true});
    editor.say(MessageType.state, {
      'camera': 'A Cam', 'support': 'Tripod', 'operating': false, 'lens': '35 mm', 'tStop': 'T2.8', 'focus': '2.4 m',
      'marks': ['Mark 1: 4.5 m'], 'warnings': [],
    });
    await tester.pumpAndSettle();
    expect(find.text('A Cam'), findsOneWidget);
    expect(find.text('35 mm · T2.8 · 2.4 m'), findsOneWidget);
    expect(find.text('Waiting for the picture…'), findsOneWidget);

    // Every control is a target of 48 or more each way.
    for (final button in tester.widgetList<ButtonStyleButton>(find.bySubtype<ButtonStyleButton>())) {
      final size = tester.getSize(find.byWidget(button));
      expect(size.width >= minTarget && size.height >= minTarget, isTrue, reason: '${(button.child as Text?)?.data} is $size');
    }

    await tester.tap(find.text('Operate'));
    await tester.tap(find.text('Lens +'));
    await tester.tap(find.text('Rack to Mark'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mark 1: 4.5 m'));
    await tester.pumpAndSettle();
    expect(editor.controls, [
      {'action': 'operate'},
      {'action': 'lens', 'step': 1},
      {'action': 'rack', 'mark': 0},
    ]);

    editor.say(MessageType.state, {'camera': 'A Cam', 'operating': true, 'live': true, 'recording': true, 'takeSeconds': 3.2, 'warnings': ['Pan peaks at 200°/s; this tripod manages 120°/s.']});
    editor.say(MessageType.result, {'action': 'bake', 'ok': false, 'message': 'Stop recording first.'});
    await tester.pumpAndSettle();
    expect(find.text('Recording 3.2 s'), findsOneWidget);
    expect(find.text('Stop'), findsOneWidget);
    expect(find.textContaining('Pan peaks', skipOffstage: false), findsOneWidget, reason: 'further down the rail');
    expect(find.text('Stop recording first.'), findsOneWidget, reason: 'a refused control says why');
    await tester.tap(find.text('Stop'));
    expect(editor.controls.last, {'action': 'stop'});

    // On a tablet the whole rail fits; on a phone it scrolls to Disconnect.
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Disconnect'));
    await tester.pumpAndSettle();
    expect(find.text('Editors on this network'), findsOneWidget);
    controller.dispose();
    // The link let go of the stream at Disconnect; closing it only tidies up.
    unawaited(editor.incoming.close());
  }, timeout: const Timeout(Duration(seconds: 60)));
}
