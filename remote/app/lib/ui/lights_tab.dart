import 'package:flutter/material.dart';

import 'common.dart';

/// lighting/PROTOCOL.md: pattern ids, in the radio's order
const patterns = [
  'Off', 'Solid', 'Rainbow', 'Chase', 'Breathe', 'Gradient', 'Twinkle', 'Comet', 'VU Meter (music)',
  'Spectrum (music)', 'Beat Pulse (music)', 'Fire', 'Sparkle', 'Scanner', 'Wipe', 'Wave', 'Fade', 'Strobe',
  'Theater', 'Rainbow Runner',
];

const zones = {'F': 'Interior front', 'R': 'Interior rear', 'E': 'Exterior'};

const swatches = [
  Color(0xFFFFFFFF), Color(0xFFFF0000), Color(0xFFFF7A00), Color(0xFFFFD000), Color(0xFF00FF40),
  Color(0xFF00E5FF), Color(0xFF0050FF), Color(0xFF9000FF), Color(0xFFFF00A0),
];

class LightsTab extends StatelessWidget {
  const LightsTab({super.key});

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final status = asMap(radio.lighting['status']);
    final looks = asMap(radio.lighting['zones']);
    return ListView(children: [
      if (status.isNotEmpty && status['connected'] != true)
        const Padding(
          padding: EdgeInsets.fromLTRB(16, 16, 16, 0),
          child: Card(child: ListTile(leading: Icon(Icons.link_off), title: Text('The light controller isn\'t connected to the radio'))),
        ),
      if (looks.isEmpty)
        const Padding(padding: EdgeInsets.all(24), child: Text('The radio\'s lighting service isn\'t running.')),
      for (final z in zones.keys)
        if (looks.containsKey(z)) _Zone(zone: z, look: asMap(looks[z])),
    ]);
  }
}

class _Zone extends StatefulWidget {
  final String zone;
  final Map<String, dynamic> look;
  const _Zone({required this.zone, required this.look});
  @override
  State<_Zone> createState() => _ZoneState();
}

class _ZoneState extends State<_Zone> {
  double? _brightness, _speed;
  int _lastPattern = 1;

  void _set(Map<String, dynamic> look) {
    final radio = RadioScope.read(context);
    act(context, () => radio.call('lighting.set', {'zone': widget.zone, 'look': look}));
  }

  @override
  Widget build(BuildContext context) {
    final look = widget.look;
    final pattern = asInt(look['pattern']);
    if (pattern != 0) _lastPattern = pattern;
    final color = parseColor(look['color']);
    final on = pattern != 0;
    return SectionCard(
      title: zones[widget.zone],
      trailing: Switch(value: on, onChanged: (v) => _set({'pattern': v ? _lastPattern : 0})),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Wrap(spacing: 10, runSpacing: 10, children: [
          for (final c in swatches)
            InkWell(
              customBorder: const CircleBorder(),
              onTap: () => _set({'color': colorHex(c), if (!on) 'pattern': 1}),
              child: Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: c,
                  shape: BoxShape.circle,
                  border: Border.all(
                    width: colorHex(c) == colorHex(color) ? 3 : 1,
                    color: colorHex(c) == colorHex(color) ? Theme.of(context).colorScheme.primary : Colors.black26,
                  ),
                ),
              ),
            ),
        ]),
        const SizedBox(height: 8),
        // Any colour: the hue
        Row(children: [
          const Icon(Icons.palette_outlined),
          Expanded(
            child: Slider(
              value: HSVColor.fromColor(color).hue,
              max: 360,
              onChangeEnd: (h) => _set({'color': colorHex(HSVColor.fromAHSV(1, h, 1, 1).toColor())}),
              onChanged: (_) {},
            ),
          ),
        ]),
        Row(children: [
          const Icon(Icons.brightness_6_outlined),
          Expanded(
            child: Slider(
              value: (_brightness ?? asDouble(look['brightness'], 255)).clamp(0, 255),
              max: 255,
              onChanged: (v) => setState(() => _brightness = v),
              onChangeEnd: (v) {
                _set({'brightness': v.round()});
                setState(() => _brightness = null);
              },
            ),
          ),
        ]),
        Row(children: [
          const Icon(Icons.speed),
          Expanded(
            child: Slider(
              value: (_speed ?? asDouble(look['speed'], 128)).clamp(0, 255),
              max: 255,
              onChanged: (v) => setState(() => _speed = v),
              onChangeEnd: (v) {
                _set({'speed': v.round()});
                setState(() => _speed = null);
              },
            ),
          ),
        ]),
        DropdownButtonFormField<int>(
          value: pattern.clamp(0, patterns.length - 1),
          decoration: const InputDecoration(labelText: 'Pattern', border: OutlineInputBorder()),
          items: [for (var i = 0; i < patterns.length; i++) DropdownMenuItem(value: i, child: Text(patterns[i]))],
          onChanged: (v) => v == null ? null : _set({'pattern': v}),
        ),
      ]),
    );
  }
}
