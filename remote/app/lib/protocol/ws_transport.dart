import 'dart:async';
import 'dart:io';

import 'transport.dart';

/// The radio over Wi-Fi: a WebSocket on port 54546 (the radio's hotspot: 10.42.0.1).
class WsTransport implements Transport {
  static const port = 54546;
  final WebSocket _socket;
  final _messages = StreamController<String>.broadcast();
  final _done = Completer<void>();

  WsTransport._(this._socket) {
    _socket.pingInterval = const Duration(seconds: 15);
    _socket.listen(
      (data) => _messages.add(data is String ? data : String.fromCharCodes(data as List<int>)),
      onDone: _finish,
      onError: (_) => _finish(),
      cancelOnError: true,
    );
  }

  static Future<WsTransport> connect(String host, {Duration timeout = const Duration(seconds: 6)}) async {
    final socket = await WebSocket.connect('ws://$host:$port/').timeout(timeout);
    return WsTransport._(socket);
  }

  void _finish() {
    if (!_done.isCompleted) _done.complete();
    _messages.close();
  }

  @override
  Stream<String> get messages => _messages.stream;
  @override
  Future<void> get done => _done.future;
  @override
  String get kind => 'Wi-Fi';

  @override
  Future<void> send(String message) async => _socket.add(message);

  @override
  Future<void> close() async {
    await _socket.close();
    _finish();
  }
}
