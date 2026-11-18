import 'dart:convert';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// Radios this phone has paired with: their name (the radio's hostname),
/// token (kept in the Keychain / Keystore), and how to reach them.
class SavedRadio {
  final String name;
  final String token;
  final String? host;   // Wi-Fi address
  final String? bleId;  // Bluetooth device id
  SavedRadio({required this.name, required this.token, this.host, this.bleId});

  Map<String, dynamic> toJson() => {'name': name, 'token': token, 'host': host, 'ble': bleId};
  static SavedRadio fromJson(Map<String, dynamic> j) =>
      SavedRadio(name: j['name'], token: j['token'], host: j['host'], bleId: j['ble']);

  SavedRadio copyWith({String? host, String? bleId}) =>
      SavedRadio(name: name, token: token, host: host ?? this.host, bleId: bleId ?? this.bleId);
}

class RadioStore {
  static const _key = 'radios';
  final _storage = const FlutterSecureStorage();

  Future<List<SavedRadio>> load() async {
    final text = await _storage.read(key: _key);
    if (text == null) return [];
    try {
      return [for (final j in jsonDecode(text) as List) SavedRadio.fromJson(j as Map<String, dynamic>)];
    } catch (_) {
      return [];
    }
  }

  Future<void> save(SavedRadio radio) async {
    final all = (await load()).where((r) => r.name != radio.name).toList()..insert(0, radio);
    await _storage.write(key: _key, value: jsonEncode([for (final r in all) r.toJson()]));
  }

  Future<void> forget(String name) async {
    final all = (await load()).where((r) => r.name != name).toList();
    await _storage.write(key: _key, value: jsonEncode([for (final r in all) r.toJson()]));
  }

  Future<SavedRadio?> find(String name) async {
    for (final r in await load()) {
      if (r.name == name) return r;
    }
    return null;
  }
}
