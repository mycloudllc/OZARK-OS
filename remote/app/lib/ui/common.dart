import 'package:flutter/material.dart';

import '../radio_model.dart';

/// The radio, for every screen below it; rebuilds them when it changes.
class RadioScope extends InheritedNotifier<RadioModel> {
  const RadioScope({super.key, required RadioModel super.notifier, required super.child});

  static RadioModel of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<RadioScope>()!.notifier!;
  static RadioModel read(BuildContext context) =>
      context.getInheritedWidgetOfExactType<RadioScope>()!.notifier!;
}

/// Runs a call to the radio; a refusal shows as a snackbar with the radio's reason.
Future<T?> act<T>(BuildContext context, Future<T> Function() f, {String? done}) async {
  final messenger = ScaffoldMessenger.of(context);
  try {
    final r = await f();
    if (done != null) messenger.showSnackBar(SnackBar(content: Text(done)));
    return r;
  } catch (e) {
    messenger.showSnackBar(SnackBar(content: Text('$e')));
    return null;
  }
}

Map<String, dynamic> asMap(Object? v) => v is Map ? Map<String, dynamic>.from(v) : <String, dynamic>{};
List<dynamic> asList(Object? v) => v is List ? v : const [];
double asDouble(Object? v, [double d = 0]) => v is num ? v.toDouble() : double.tryParse('$v') ?? d;
int asInt(Object? v, [int d = 0]) => v is num ? v.toInt() : int.tryParse('$v') ?? d;

Color parseColor(Object? hex, [Color fallback = Colors.white]) {
  final s = '$hex'.replaceFirst('#', '');
  final v = int.tryParse(s, radix: 16);
  if (s.length != 6 || v == null) return fallback;
  return Color(0xFF000000 | v);
}

String colorHex(Color c) =>
    '#${[c.r, c.g, c.b].map((v) => (v * 255).round().toRadixString(16).padLeft(2, '0')).join()}';

class SectionCard extends StatelessWidget {
  final String? title;
  final Widget child;
  final Widget? trailing;
  const SectionCard({super.key, this.title, required this.child, this.trailing});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.fromLTRB(16, 8, 16, 8),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (title != null)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Row(children: [
                  Expanded(child: Text(title!, style: Theme.of(context).textTheme.titleMedium)),
                  if (trailing != null) trailing!,
                ]),
              ),
            child,
          ],
        ),
      ),
    );
  }
}
