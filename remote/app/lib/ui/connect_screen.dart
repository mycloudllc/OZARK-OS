import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../protocol/ble_transport.dart';
import '../protocol/ws_transport.dart';
import '../radio_model.dart';
import 'common.dart';

/// Find the radio: nearby over Bluetooth, or at an address on its Wi-Fi.
class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key});
  @override
  State<ConnectScreen> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> {
  final _host = TextEditingController(text: '10.42.0.1');
  List<ScanResult> _found = [];
  StreamSubscription? _scan, _adapter;
  BluetoothAdapterState _bt = BluetoothAdapterState.unknown;
  bool _scanning = false;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _adapter = FlutterBluePlus.adapterState.listen((s) {
      setState(() => _bt = s);
      if (s == BluetoothAdapterState.on && _found.isEmpty) _startScan();
    });
    _reconnectLast();
  }

  // The radio this phone used last: straight back to it over Wi-Fi if it's reachable
  Future<void> _reconnectLast() async {
    final radio = RadioScope.read(context);
    final saved = await radio.store.load();
    if (saved.isEmpty || saved.first.host == null || !mounted) return;
    _host.text = saved.first.host!;
  }

  void _startScan() {
    _scan?.cancel();
    setState(() {
      _scanning = true;
      _found = [];
    });
    _scan = BleTransport.scan().listen((r) => setState(() => _found = r));
    Future.delayed(const Duration(seconds: 15), () {
      if (mounted) setState(() => _scanning = false);
    });
  }

  Future<void> _connectBle(ScanResult r) async {
    setState(() => _busy = true);
    final radio = RadioScope.read(context);
    try {
      final t = await BleTransport.connect(r.device);
      await radio.attach(t, bleId: r.device.remoteId.str);
    } catch (e) {
      if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Couldn\'t connect: $e')));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _connectWifi() async {
    setState(() => _busy = true);
    final radio = RadioScope.read(context);
    final host = _host.text.trim();
    try {
      final t = await WsTransport.connect(host);
      await radio.attach(t, host: host);
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
            content: Text('No radio at $host. Join the radio\'s Wi-Fi first (Settings > Network on the radio shows its name).')));
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  void dispose() {
    _scan?.cancel();
    _adapter?.cancel();
    BleTransport.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(title: const Text('Ozark Remote')),
      body: ListView(
        children: [
          if (radio.error != null && radio.link == LinkState.disconnected)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
              child: Text(radio.error!, style: TextStyle(color: theme.colorScheme.error)),
            ),
          SectionCard(
            title: 'Nearby radios (Bluetooth)',
            trailing: _scanning
                ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                : IconButton(icon: const Icon(Icons.refresh), onPressed: _bt == BluetoothAdapterState.on ? _startScan : null),
            child: Column(children: [
              if (_bt != BluetoothAdapterState.on)
                const ListTile(leading: Icon(Icons.bluetooth_disabled), title: Text('Turn on Bluetooth to find the radio')),
              if (_bt == BluetoothAdapterState.on && _found.isEmpty)
                ListTile(
                  leading: const Icon(Icons.bluetooth_searching),
                  title: Text(_scanning ? 'Looking for radios…' : 'No radio found'),
                  subtitle: const Text('Stay close to the vehicle with the radio on'),
                ),
              for (final r in _found)
                ListTile(
                  leading: const Icon(Icons.radio),
                  title: Text(r.advertisementData.advName.isNotEmpty ? r.advertisementData.advName : r.device.remoteId.str),
                  subtitle: Text('Signal ${r.rssi} dBm'),
                  trailing: FilledButton(onPressed: _busy ? null : () => _connectBle(r), child: const Text('Connect')),
                ),
            ]),
          ),
          SectionCard(
            title: 'On the radio\'s Wi-Fi',
            child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              const Text('Join the radio\'s hotspot in your phone\'s Wi-Fi settings, then connect.'),
              const SizedBox(height: 12),
              Row(children: [
                Expanded(
                  child: TextField(
                    controller: _host,
                    keyboardType: TextInputType.url,
                    decoration: const InputDecoration(labelText: 'Radio address', border: OutlineInputBorder()),
                  ),
                ),
                const SizedBox(width: 12),
                FilledButton(onPressed: _busy ? null : _connectWifi, child: const Text('Connect')),
              ]),
            ]),
          ),
          if (_busy) const Padding(padding: EdgeInsets.all(16), child: Center(child: CircularProgressIndicator())),
        ],
      ),
    );
  }
}
