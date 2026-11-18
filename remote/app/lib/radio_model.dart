import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/foundation.dart';

import 'protocol/client.dart';
import 'protocol/transport.dart';
import 'storage.dart';

enum LinkState { disconnected, connecting, needsPairing, pairing, ready }

/// The connected radio and its state (PROTOCOL.md: state.get, then events).
class RadioModel extends ChangeNotifier {
  final store = RadioStore();
  RemoteClient? _client;
  LinkState link = LinkState.disconnected;
  String radioName = '';
  String? error;
  String? _host, _bleId;

  Map<String, dynamic> dash = {};
  Map<String, dynamic> lighting = {};
  Map<String, dynamic> vehicle = {};
  Map<String, dynamic> dtcsCleared = {};
  /// When dash (with media positions) last arrived: positions count on from here
  DateTime dashAt = DateTime.now();
  /// Imperial units (mph, °F, psi) or metric
  bool imperial = true;

  String get transportKind => _client?.transport.kind ?? '';
  bool get ready => link == LinkState.ready;

  /// Opens the link, asks who it is, and signs in if this phone knows it.
  Future<void> attach(Transport transport, {String? host, String? bleId}) async {
    await detach();
    _host = host;
    _bleId = bleId;
    error = null;
    link = LinkState.connecting;
    notifyListeners();
    final client = RemoteClient(transport);
    _client = client;
    client.events.listen(_event);
    transport.done.then((_) {
      if (_client == client) {
        _client = null;
        link = LinkState.disconnected;
        error ??= 'Disconnected from the radio';
        notifyListeners();
      }
    });
    try {
      final info = await client.call('info') as Map<String, dynamic>;
      radioName = '${info['name']}';
      final saved = await store.find(radioName);
      if (saved != null) {
        try {
          await client.call('hello', {'token': saved.token});
          await store.save(saved.copyWith(host: host, bleId: bleId));
          await _ready();
          return;
        } on RemoteError catch (e) {
          if (e.code != 'unauthorized') rethrow;
          await store.forget(radioName);  // removed on the radio: pair again
        }
      }
      link = LinkState.needsPairing;
      notifyListeners();
    } catch (e) {
      error = '$e';
      await detach();
    }
  }

  /// Puts a code on the radio's screen.
  Future<void> requestPairing(String phoneName) async {
    error = null;
    await _client!.call('pair.request', {
      'name': phoneName,
      'platform': Platform.isIOS ? 'ios' : Platform.isAndroid ? 'android' : Platform.operatingSystem,
    });
    link = LinkState.pairing;
    notifyListeners();
  }

  /// Back to asking for a code (the radio ended the last attempt).
  void restartPairing() {
    link = LinkState.needsPairing;
    notifyListeners();
  }

  /// Sends the code from the screen; throws RemoteError (wrong_code ...) if refused.
  Future<void> confirmPairing(String code) async {
    final r = await _client!.call('pair.confirm', {'code': code}) as Map<String, dynamic>;
    await store.save(SavedRadio(name: radioName, token: r['token'] as String, host: _host, bleId: _bleId));
    await _ready();
  }

  Future<void> _ready() async {
    final s = await _client!.call('state.get') as Map<String, dynamic>;
    dash = Map<String, dynamic>.from(s['dash'] ?? {});
    dashAt = DateTime.now();
    lighting = Map<String, dynamic>.from(s['lighting'] ?? {});
    vehicle = Map<String, dynamic>.from(s['vehicle'] ?? {});
    link = LinkState.ready;
    notifyListeners();
  }

  void _event(MapEntry<String, dynamic> e) {
    final data = e.value;
    switch (e.key) {
      case 'dash.state':
        dash = Map<String, dynamic>.from(data);
        dashAt = DateTime.now();
      case 'lighting.status':
        lighting = {...lighting, 'status': data};
      case 'lighting.look':
        final zones = Map<String, dynamic>.from(lighting['zones'] ?? {});
        zones['${data['zone']}'] = data['look'];
        lighting = {...lighting, 'zones': zones};
      case 'vehicle.status':
        vehicle = {...vehicle, 'status': data};
      case 'vehicle.values':
        vehicle = {...vehicle, 'values': data};
      case 'vehicle.tires':
        vehicle = {...vehicle, 'tires': data};
      case 'vehicle.dtcs':
        vehicle = {...vehicle, 'dtcs': data};
      case 'vehicle.dtcs_cleared':
        dtcsCleared = Map<String, dynamic>.from(data);
      default:
        return;
    }
    notifyListeners();
  }

  /// A method call; the error's message is readable (shown in a snackbar).
  Future<dynamic> call(String method, [Map<String, dynamic>? params]) {
    final c = _client;
    if (c == null) return Future.error(RemoteError('closed', 'Not connected to the radio'));
    return c.call(method, params, method == 'system.update.check' ? const Duration(minutes: 2) : const Duration(seconds: 20));
  }

  Future<void> detach() async {
    final c = _client;
    _client = null;
    if (c != null) await c.close();
    link = LinkState.disconnected;
    notifyListeners();
  }

  void setImperial(bool v) {
    imperial = v;
    notifyListeners();
  }

  Future<void> forgetRadio() async {
    await store.forget(radioName);
    await detach();
  }
}
