// ignore_for_file: avoid_print
// Talks to a real radio over Wi-Fi with the app's own protocol code:
//   dart run tool/smoke.dart <host> <token>
import 'dart:io';

import 'package:ozark_remote/protocol/client.dart';
import 'package:ozark_remote/protocol/ws_transport.dart';

Future<void> main(List<String> args) async {
  final c = RemoteClient(await WsTransport.connect(args[0]));
  final events = <String>{};
  c.events.listen((e) => events.add(e.key));
  print('info  ${await c.call('info')}');
  print('hello ${(await c.call('hello', {'token': args[1]}))['client_id']}');
  final s = await c.call('state.get') as Map;
  print('state ${s.keys.toList()} volume=${s['dash']['volume']} radio=${s['dash']['media']['radio']['label']}');
  print('volume ${await c.call('dash.volume', {'value': 25})}');
  print('tune   ${await c.call('radio.tune', {'value': 1003})}');
  print('light  ${await c.call('lighting.set', {'zone': 'F', 'look': {'color': '#00ff40', 'pattern': 2}})}');
  try {
    await c.call('dash.theme', {'mode': 'sepia'});
  } on RemoteError catch (e) {
    print('error  ${e.code}: ${e.message}');
  }
  await Future.delayed(const Duration(seconds: 1));
  final after = await c.call('state.get') as Map;
  print('after  volume=${after['dash']['volume']} radio=${after['dash']['media']['radio']['label']} F=${after['lighting']['zones']['F']['color']}');
  print('events $events');
  await c.close();
  exit(0);
}
