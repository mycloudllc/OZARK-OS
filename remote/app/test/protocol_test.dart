import 'dart:async';
import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:ozark_remote/protocol/client.dart';
import 'package:ozark_remote/protocol/framing.dart';
import 'package:ozark_remote/protocol/transport.dart';

/// A radio in memory: answers what the test tells it to
class FakeTransport implements Transport {
  final _in = StreamController<String>.broadcast();
  final _done = Completer<void>();
  final sent = <Map<String, dynamic>>[];
  void Function(Map<String, dynamic>)? onSend;
  void push(Object message) => _in.add(jsonEncode(message));
  @override
  Stream<String> get messages => _in.stream;
  @override
  Future<void> get done => _done.future;
  @override
  String get kind => 'test';
  @override
  Future<void> send(String m) async {
    final j = jsonDecode(m) as Map<String, dynamic>;
    sent.add(j);
    onSend?.call(j);
  }
  @override
  Future<void> close() async {
    if (!_done.isCompleted) _done.complete();
  }
}

void main() {
  group('LineFramer', () {
    test('splits to the MTU and joins back', () {
      final msg = jsonEncode({'id': 1, 'method': 'state.get', 'params': {'x': 'ü' * 100}});
      final pieces = LineFramer.split(msg, 23);
      expect(pieces.every((p) => p.length <= 20), isTrue);
      final f = LineFramer();
      final out = <String>[];
      for (final p in pieces) {
        out.addAll(f.add(p));
      }
      expect(out, [msg]);
    });
    test('several messages in one piece, one across pieces', () {
      final f = LineFramer();
      expect(f.add(utf8.encode('{"a":1}\n{"b":')), ['{"a":1}']);
      expect(f.add(utf8.encode('2}\n')), ['{"b":2}']);
    });
    test('large MTU is capped at 512', () {
      expect(LineFramer.split('x' * 2000, 1000).first.length, 512);
    });
  });

  group('RemoteClient', () {
    test('matches replies to requests, raises errors, passes events', () async {
      final t = FakeTransport();
      final c = RemoteClient(t);
      t.onSend = (j) {
        if (j['method'] == 'dash.volume') {
          t.push({'event': 'dash.state', 'data': {'volume': 30}});
          t.push({'id': j['id'], 'result': {'ok': true}});
        } else {
          t.push({'id': j['id'], 'error': {'code': 'bad_params', 'message': 'nope'}});
        }
      };
      final events = <String>[];
      c.events.listen((e) => events.add(e.key));
      expect(await c.call('dash.volume', {'value': 30}), {'ok': true});
      await expectLater(c.call('radio.band', {'band': 'x'}), throwsA(isA<RemoteError>().having((e) => e.code, 'code', 'bad_params')));
      expect(events, ['dash.state']);
      expect(t.sent.first, {'id': 1, 'method': 'dash.volume', 'params': {'value': 30}});
    });
    test('pending calls fail when the link closes', () async {
      final t = FakeTransport();
      final c = RemoteClient(t);
      final f = c.call('state.get');
      await t.close();
      await expectLater(f, throwsA(isA<RemoteError>()));
    });
  });
}
