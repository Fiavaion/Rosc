// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// The connection to a Rosc editor: hello, pairing, then motion and controls out, picture and
/// state in.
library;

import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';

import 'protocol.dart';

enum LinkPhase { pairing, paired, closed }

/// The editor's answer to a control.
class ControlResult {
  const ControlResult(this.action, this.ok, this.message);
  final String action;
  final bool ok;
  final String message;
}

class EditorLink extends ChangeNotifier {
  /// A link over any byte stream; [connect] makes one over the network.
  EditorLink(Stream<List<int>> incoming, this._send, {required String deviceName, String appVersion = '', this.onClose}) {
    _subscription = incoming.listen(_receive, onDone: () => _closed(null), onError: (Object _) => _closed(null));
    _send(encodeJson(MessageType.hello, {'protocol': protocolVersion, 'device': deviceName, 'app': appVersion}));
  }

  static Future<EditorLink> connect(String host, {int port = serverPort, required String deviceName, String appVersion = ''}) async {
    final socket = await Socket.connect(host, port, timeout: const Duration(seconds: 5));
    socket.setOption(SocketOption.tcpNoDelay, true);
    return EditorLink(socket, socket.add, deviceName: deviceName, appVersion: appVersion, onClose: socket.destroy);
  }

  final void Function(List<int>) _send;
  /// Called once the link has closed, to let go of what carried it.
  final void Function()? onClose;
  late final StreamSubscription<List<int>> _subscription;
  final MessageReader _reader = MessageReader();

  LinkPhase phase = LinkPhase.pairing;

  /// What the editor calls itself, once it has said.
  String editor = '';

  /// Why pairing was refused or the link closed, to show the user.
  String problem = '';

  CameraState state = const CameraState();

  /// The newest picture of the camera, apart from the rest so a frame redraws only the picture.
  final ValueNotifier<Uint8List?> picture = ValueNotifier(null);

  /// The newest answer to a control.
  final ValueNotifier<ControlResult?> result = ValueNotifier(null);

  bool get isPaired => phase == LinkPhase.paired;

  void pair(String code) {
    if (phase == LinkPhase.pairing) {
      problem = '';
      _send(encodeJson(MessageType.pair, {'code': code}));
      notifyListeners();
    }
  }

  void control(String action, [Map<String, Object?> extra = const {}]) {
    if (isPaired) {
      _send(encodeJson(MessageType.control, {'action': action, ...extra}));
    }
  }

  void sendMotion(Motion motion) {
    if (isPaired) {
      _send(encodeMessage(MessageType.motion, motion.toBytes()));
    }
  }

  void close() => _closed('');

  void _receive(List<int> bytes) {
    _reader.add(bytes);
    for (var message = _reader.next(); message != null; message = _reader.next()) {
      _handle(message);
    }
    if (_reader.isBroken) {
      _closed('The editor sent something this app cannot read. Update RoscCam and Rosc to the same version.');
    }
  }

  void _handle(Message message) {
    switch (message.type) {
      case MessageType.welcome:
        final json = message.json ?? const {};
        editor = json['editor'] is String ? json['editor']! as String : 'Rosc';
        final version = json['protocol'];
        if (version != protocolVersion) {
          _closed('$editor speaks RoscCam protocol $version and this app speaks $protocolVersion. Update the older of the two.');
          return;
        }
        notifyListeners();
      case MessageType.paired:
        final json = message.json ?? const {};
        if (json['paired'] == true) {
          phase = LinkPhase.paired;
          problem = '';
        } else {
          problem = json['reason'] is String ? json['reason']! as String : 'The editor did not pair.';
        }
        notifyListeners();
      case MessageType.state:
        final json = message.json;
        if (json != null) {
          state = CameraState.fromJson(json);
          notifyListeners();
        }
      case MessageType.picture:
        picture.value = message.body;
      case MessageType.result:
        final json = message.json ?? const {};
        result.value = ControlResult(
          json['action'] is String ? json['action']! as String : '',
          json['ok'] == true,
          json['message'] is String ? json['message']! as String : '',
        );
      default:
        // A newer editor's extras are let pass.
        break;
    }
  }

  /// Ends the link. [reason] null means the editor ended it.
  void _closed(String? reason) {
    if (phase == LinkPhase.closed) {
      return;
    }
    phase = LinkPhase.closed;
    if (reason != null && reason.isNotEmpty) {
      problem = reason;
    } else if (reason == null && problem.isEmpty) {
      problem = 'The editor ended the connection: it was unpaired, or its RoscCam server stopped.';
    }
    _subscription.cancel();
    onClose?.call();
    notifyListeners();
  }

  @override
  void dispose() {
    _closed('');
    picture.dispose();
    result.dispose();
    super.dispose();
  }
}
