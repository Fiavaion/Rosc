// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

/// Finds the Rosc editors on the network by the announcement each sends once a second.
library;

import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import 'protocol.dart';

class FoundEditor {
  FoundEditor(this.name, this.address, this.port, this.seen);
  final String name;
  final String address;
  final int port;
  DateTime seen;
}

class EditorFinder extends ChangeNotifier {
  RawDatagramSocket? _socket;
  final Map<String, FoundEditor> _found = {};

  /// Why the finder cannot listen, to show the user; empty while it can.
  String problem = '';

  /// The editors heard from in the last few seconds, by name.
  List<FoundEditor> get editors {
    final recent = DateTime.now().subtract(const Duration(seconds: 4));
    return _found.values.where((editor) => editor.seen.isAfter(recent)).toList()..sort((a, b) => a.name.compareTo(b.name));
  }

  Future<void> start() async {
    if (_socket != null) {
      return;
    }
    try {
      final socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, discoveryPort, reuseAddress: true);
      _socket = socket;
      socket.listen((event) {
        if (event != RawSocketEvent.read) {
          return;
        }
        final datagram = socket.receive();
        if (datagram != null) {
          _heard(datagram);
        }
      });
      problem = '';
    } on SocketException catch (error) {
      problem = 'Cannot listen for editors on the network (${error.message}). Type the address instead.';
    }
    notifyListeners();
  }

  void stop() {
    _socket?.close();
    _socket = null;
  }

  void _heard(Datagram datagram) {
    Object? json;
    try {
      json = jsonDecode(utf8.decode(datagram.data));
    } on FormatException {
      return;
    }
    if (json is! Map || json['rosccam'] != protocolVersion) {
      return;
    }
    final address = datagram.address.address;
    final name = json['name'] is String ? json['name'] as String : address;
    final port = json['port'] is int ? json['port'] as int : serverPort;
    final known = _found[address];
    if (known != null && known.name == name && known.port == port) {
      known.seen = DateTime.now();
    } else {
      _found[address] = FoundEditor(name, address, port, DateTime.now());
    }
    notifyListeners();
  }

  @override
  void dispose() {
    stop();
    super.dispose();
  }
}
