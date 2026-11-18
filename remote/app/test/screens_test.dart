// Renders the app's screens with real fonts into test/screens/*.png, from a
// state the radio answered (test/fixtures/state.json; car, phone and music
// values filled in by hand). Run: flutter test test/screens_test.dart --update-goldens
@Tags(['screens'])
library;

import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ozark_remote/main.dart';
import 'package:ozark_remote/radio_model.dart';

Future<void> loadFont(String family, List<String> files) async {
  final loader = FontLoader(family);
  for (final f in files) {
    loader.addFont(Future.value(ByteData.sublistView(File(f).readAsBytesSync())));
  }
  await loader.load();
}

RadioModel model(Map<String, dynamic> s, {String active = 'radio', bool dark = false}) {
  final m = RadioModel()
    ..link = LinkState.ready
    ..radioName = 'ozark-3b'
    ..dash = Map<String, dynamic>.from(s['dash'])
    ..lighting = Map<String, dynamic>.from(s['lighting'])
    ..vehicle = Map<String, dynamic>.from(s['vehicle'])
    ..dashAt = DateTime.now();
  m.dash['media'] = {...m.dash['media'], 'active': active};
  return m;
}

void main() {
  final fonts = '${Platform.environment['FLUTTER_ROOT']}/bin/cache/artifacts/material_fonts';
  setUpAll(() async {
    await loadFont('Roboto', ['$fonts/Roboto-Regular.ttf', '$fonts/Roboto-Medium.ttf', '$fonts/Roboto-Bold.ttf']);
    await loadFont('MaterialIcons', ['$fonts/MaterialIcons-Regular.otf']);
  });
  final state = jsonDecode(File('test/fixtures/state.json').readAsStringSync()) as Map<String, dynamic>;

  Future<void> shoot(WidgetTester tester, RadioModel m, String name, {int tab = 0, ThemeMode mode = ThemeMode.light}) async {
    tester.view.physicalSize = const Size(1080, 2340);
    tester.view.devicePixelRatio = 2.625;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(OzarkRemoteApp(model: m, themeMode: mode));
    await tester.pump();
    if (tab > 0) {
      await tester.tap(find.byType(NavigationDestination).at(tab));
      await tester.pump(const Duration(milliseconds: 500));
    }
    await tester.pump(const Duration(milliseconds: 100));
    await expectLater(find.byType(MaterialApp), matchesGoldenFile('screens/$name.png'));
  }

  testWidgets('media: radio', (t) => shoot(t, model(state), '1-media-radio'));
  testWidgets('media: bluetooth', (t) => shoot(t, model(state, active: 'bluetooth'), '2-media-bluetooth'));
  testWidgets('lights', (t) => shoot(t, model(state), '3-lights', tab: 1));
  testWidgets('vehicle', (t) => shoot(t, model(state), '4-vehicle', tab: 2));
  testWidgets('radio settings', (t) => shoot(t, model(state), '5-radio', tab: 3));
  testWidgets('media dark', (t) => shoot(t, model(state, active: 'bluetooth'), '6-media-dark', mode: ThemeMode.dark));
  testWidgets('connect', (t) async {
    final m = RadioModel();
    await shoot(t, m, '7-connect');
  });
  testWidgets('pairing', (t) async {
    final m = RadioModel()
      ..link = LinkState.pairing
      ..radioName = 'ozark-3b';
    await shoot(t, m, '8-pair');
  });
}
