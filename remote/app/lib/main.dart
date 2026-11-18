import 'package:dynamic_color/dynamic_color.dart';
import 'package:flutter/material.dart';

import 'radio_model.dart';
import 'ui/common.dart';
import 'ui/connect_screen.dart';
import 'ui/home_screen.dart';
import 'ui/pair_screen.dart';

void main() {
  runApp(OzarkRemoteApp(model: RadioModel()));
}

class OzarkRemoteApp extends StatelessWidget {
  final RadioModel model;
  final ThemeMode themeMode;
  const OzarkRemoteApp({super.key, required this.model, this.themeMode = ThemeMode.system});

  // Ozark brand orange (the About page's "TECHNOLOGIES"): the accent where the
  // phone doesn't offer one (iPhone, Android before 12)
  static const brand = Color(0xFFC8602B);

  @override
  Widget build(BuildContext context) {
    // The phone's own accent colour (Material You on Android 12+)
    return DynamicColorBuilder(builder: (ColorScheme? light, ColorScheme? dark) {
      final lightScheme = light?.harmonized() ?? ColorScheme.fromSeed(seedColor: brand);
      final darkScheme = dark?.harmonized() ?? ColorScheme.fromSeed(seedColor: brand, brightness: Brightness.dark);
      return RadioScope(
        notifier: model,
        child: MaterialApp(
          title: 'Ozark Remote',
          debugShowCheckedModeBanner: false,
          theme: ThemeData(colorScheme: lightScheme, useMaterial3: true),
          darkTheme: ThemeData(colorScheme: darkScheme, useMaterial3: true),
          themeMode: themeMode,
          home: const _Root(),
        ),
      );
    });
  }
}

class _Root extends StatelessWidget {
  const _Root();

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    return switch (radio.link) {
      LinkState.ready => const HomeScreen(),
      LinkState.needsPairing || LinkState.pairing => const PairScreen(),
      _ => const ConnectScreen(),
    };
  }
}
