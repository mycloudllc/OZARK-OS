import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'framing.dart';
import 'transport.dart';

/// The radio over Bluetooth LE (PROTOCOL.md): writes to RX, notifications
/// from TX, newline-framed JSON in MTU-sized pieces.
class BleTransport implements Transport {
  static final service = Guid('7a6f0001-5a3b-4c1e-9f2d-0c8e4b1a6d20');
  static final rxUuid = Guid('7a6f0002-5a3b-4c1e-9f2d-0c8e4b1a6d20');
  static final txUuid = Guid('7a6f0003-5a3b-4c1e-9f2d-0c8e4b1a6d20');

  final BluetoothDevice device;
  final BluetoothCharacteristic _rx;
  final BluetoothCharacteristic _tx;
  final _messages = StreamController<String>.broadcast();
  final _done = Completer<void>();
  final _framer = LineFramer();
  final List<StreamSubscription> _subs = [];
  // Writes go out one at a time, in order
  Future<void> _queue = Future.value();

  BleTransport._(this.device, this._rx, this._tx) {
    _subs.add(_tx.onValueReceived.listen((piece) {
      for (final m in _framer.add(piece)) {
        _messages.add(m);
      }
    }));
    _subs.add(device.connectionState.listen((s) {
      if (s == BluetoothConnectionState.disconnected) _finish();
    }));
  }

  /// Radios advertising the service nearby.
  static Stream<List<ScanResult>> scan({Duration timeout = const Duration(seconds: 15)}) {
    FlutterBluePlus.startScan(withServices: [service], timeout: timeout);
    return FlutterBluePlus.scanResults;
  }

  static Future<void> stopScan() => FlutterBluePlus.stopScan();

  static Future<BleTransport> connect(BluetoothDevice device) async {
    await FlutterBluePlus.stopScan();
    await device.connect(timeout: const Duration(seconds: 12), autoConnect: false);
    try {
      // Android starts at 23 bytes; iOS negotiates a large MTU by itself
      if (Platform.isAndroid) await device.requestMtu(512);
      final services = await device.discoverServices();
      final s = services.firstWhere((s) => s.uuid == service,
          orElse: () => throw StateError('This device isn\'t an Ozark radio'));
      final rx = s.characteristics.firstWhere((c) => c.uuid == rxUuid);
      final tx = s.characteristics.firstWhere((c) => c.uuid == txUuid);
      final transport = BleTransport._(device, rx, tx);
      await tx.setNotifyValue(true);
      return transport;
    } catch (_) {
      await device.disconnect();
      rethrow;
    }
  }

  void _finish() {
    if (_done.isCompleted) return;
    _done.complete();
    for (final s in _subs) {
      s.cancel();
    }
    _messages.close();
  }

  @override
  Stream<String> get messages => _messages.stream;
  @override
  Future<void> get done => _done.future;
  @override
  String get kind => 'Bluetooth';

  @override
  Future<void> send(String message) {
    _queue = _queue.then((_) async {
      for (final piece in LineFramer.split(message, device.mtuNow)) {
        await _rx.write(piece, withoutResponse: false);
      }
    }).catchError((_) {});
    return _queue;
  }

  @override
  Future<void> close() async {
    await device.disconnect();
    _finish();
  }
}
