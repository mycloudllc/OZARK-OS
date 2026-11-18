import 'dart:async';
import 'dart:convert';

import 'transport.dart';

/// An error the radio answered with (PROTOCOL.md: code and readable message).
class RemoteError implements Exception {
  final String code;
  final String message;
  RemoteError(this.code, this.message);
  @override
  String toString() => message;
}

/// Requests, replies and events over any transport.
class RemoteClient {
  final Transport transport;
  final _pending = <int, Completer<dynamic>>{};
  final _events = StreamController<MapEntry<String, dynamic>>.broadcast();
  var _next = 0;
  late final StreamSubscription<String> _sub;

  RemoteClient(this.transport) {
    _sub = transport.messages.listen(_receive);
    transport.done.then((_) {
      for (final c in _pending.values) {
        if (!c.isCompleted) c.completeError(RemoteError('closed', 'The connection to the radio closed'));
      }
      _pending.clear();
      _events.close();
    });
  }

  /// event name -> data
  Stream<MapEntry<String, dynamic>> get events => _events.stream;

  Future<dynamic> call(String method, [Map<String, dynamic>? params, Duration timeout = const Duration(seconds: 20)]) {
    final id = ++_next;
    final completer = Completer<dynamic>();
    _pending[id] = completer;
    transport.send(jsonEncode({'id': id, 'method': method, if (params != null) 'params': params}));
    return completer.future.timeout(timeout, onTimeout: () {
      _pending.remove(id);
      throw RemoteError('timeout', 'The radio didn\'t answer');
    });
  }

  void _receive(String text) {
    final Object? decoded;
    try {
      decoded = jsonDecode(text);
    } catch (_) {
      return;
    }
    if (decoded is! Map<String, dynamic>) return;
    if (decoded.containsKey('event')) {
      _events.add(MapEntry(decoded['event'] as String, decoded['data']));
      return;
    }
    final id = decoded['id'];
    final completer = id is int ? _pending.remove(id) : null;
    if (completer == null) return;
    final error = decoded['error'];
    if (error is Map) {
      completer.completeError(RemoteError('${error['code']}', '${error['message']}'));
    } else {
      completer.complete(decoded['result']);
    }
  }

  Future<void> close() async {
    await _sub.cancel();
    await transport.close();
  }
}
