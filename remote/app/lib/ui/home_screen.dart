import 'package:flutter/material.dart';

import 'common.dart';
import 'lights_tab.dart';
import 'media_tab.dart';
import 'system_tab.dart';
import 'vehicle_tab.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});
  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _tab = 0;

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text(radio.radioName),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Chip(
              avatar: Icon(radio.transportKind == 'Bluetooth' ? Icons.bluetooth_connected : Icons.wifi, size: 18),
              label: Text(radio.transportKind),
            ),
          ),
        ],
      ),
      body: IndexedStack(index: _tab, children: const [MediaTab(), LightsTab(), VehicleTab(), SystemTab()]),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.music_note_outlined), selectedIcon: Icon(Icons.music_note), label: 'Media'),
          NavigationDestination(icon: Icon(Icons.lightbulb_outline), selectedIcon: Icon(Icons.lightbulb), label: 'Lights'),
          NavigationDestination(icon: Icon(Icons.directions_car_outlined), selectedIcon: Icon(Icons.directions_car), label: 'Vehicle'),
          NavigationDestination(icon: Icon(Icons.settings_outlined), selectedIcon: Icon(Icons.settings), label: 'Radio'),
        ],
      ),
    );
  }
}
