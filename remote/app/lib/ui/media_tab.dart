import 'dart:async';

import 'package:flutter/material.dart';

import 'common.dart';

class MediaTab extends StatefulWidget {
  const MediaTab({super.key});
  @override
  State<MediaTab> createState() => _MediaTabState();
}

class _MediaTabState extends State<MediaTab> {
  double? _volume;  // while dragging
  double? _dial;
  late final Timer _tick;

  @override
  void initState() {
    super.initState();
    // Song positions count on between updates
    _tick = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _tick.cancel();
    super.dispose();
  }

  String _time(int ms) {
    final s = ms ~/ 1000;
    return '${s ~/ 60}:${(s % 60).toString().padLeft(2, '0')}';
  }

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final media = asMap(radio.dash['media']);
    final active = '${media['active'] ?? 'radio'}';
    final theme = Theme.of(context);

    return ListView(children: [
      Padding(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
        child: SegmentedButton<String>(
          segments: const [
            ButtonSegment(value: 'radio', icon: Icon(Icons.radio), label: Text('Radio')),
            ButtonSegment(value: 'bluetooth', icon: Icon(Icons.bluetooth_audio), label: Text('Bluetooth')),
            ButtonSegment(value: 'local', icon: Icon(Icons.folder), label: Text('Local')),
          ],
          selected: {active},
          onSelectionChanged: (s) => act(context, () => radio.call('media.source', {'source': s.first})),
        ),
      ),
      if (active == 'radio') _radioCard(context, asMap(media['radio'])) else _playerCard(context, active, asMap(media[active])),
      SectionCard(
        title: 'Volume',
        child: Row(children: [
          IconButton(
            icon: const Icon(Icons.volume_down),
            onPressed: () => act(context, () => radio.call('dash.volume_step', {'delta': -5})),
          ),
          Expanded(
            child: Slider(
              value: (_volume ?? asDouble(radio.dash['volume'])).clamp(0, 100),
              max: 100,
              divisions: 100,
              label: '${(_volume ?? asDouble(radio.dash['volume'])).round()}',
              onChanged: (v) => setState(() => _volume = v),
              onChangeEnd: (v) {
                act(context, () => radio.call('dash.volume', {'value': v.round()}));
                setState(() => _volume = null);
              },
            ),
          ),
          IconButton(
            icon: const Icon(Icons.volume_up),
            onPressed: () => act(context, () => radio.call('dash.volume_step', {'delta': 5})),
          ),
        ]),
      ),
      if (active == 'radio' && media['radio'] is Map && asMap(media['radio'])['available'] != true)
        Padding(
          padding: const EdgeInsets.all(16),
          child: Text('The radio has no tuner loaded (Media > Radio > ⚙ on the radio).',
              style: TextStyle(color: theme.colorScheme.onSurfaceVariant)),
        ),
    ]);
  }

  Widget _playerCard(BuildContext context, String source, Map<String, dynamic> s) {
    final radio = RadioScope.of(context);
    final theme = Theme.of(context);
    final playing = s['playing'] == true;
    final duration = asInt(s['duration_ms']);
    var position = asInt(s['position_ms']);
    if (playing) position += DateTime.now().difference(radio.dashAt).inMilliseconds;
    if (duration > 0 && position > duration) position = duration;
    final title = '${s['title'] ?? ''}';
    final subtitle = [s['artist'], s['album']].where((v) => v != null && '$v'.isNotEmpty).join(' · ');
    final available = s['available'] == true;

    return SectionCard(
      child: Column(children: [
        const SizedBox(height: 8),
        Icon(source == 'bluetooth' ? Icons.bluetooth_audio : Icons.music_note, size: 48, color: theme.colorScheme.primary),
        const SizedBox(height: 12),
        Text(title.isNotEmpty ? title : available ? 'Nothing playing' : source == 'bluetooth' ? 'No phone playing over Bluetooth' : 'No music on the radio',
            style: theme.textTheme.titleLarge, textAlign: TextAlign.center),
        if (subtitle.isNotEmpty) Text(subtitle, style: theme.textTheme.bodyMedium, textAlign: TextAlign.center),
        if (source == 'bluetooth' && '${s['device'] ?? ''}'.isNotEmpty)
          Text('from ${s['device']}', style: theme.textTheme.bodySmall),
        const SizedBox(height: 16),
        if (duration > 0) ...[
          LinearProgressIndicator(value: duration > 0 ? position / duration : 0),
          const SizedBox(height: 4),
          Row(children: [Text(_time(position)), const Spacer(), Text(_time(duration))]),
        ],
        const SizedBox(height: 8),
        Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          IconButton(
            iconSize: 40,
            icon: const Icon(Icons.skip_previous),
            onPressed: available ? () => act(context, () => radio.call('media.previous', {'source': source})) : null,
          ),
          const SizedBox(width: 16),
          IconButton.filled(
            iconSize: 48,
            icon: Icon(playing ? Icons.pause : Icons.play_arrow),
            onPressed: available ? () => act(context, () => radio.call('media.toggle', {'source': source})) : null,
          ),
          const SizedBox(width: 16),
          IconButton(
            iconSize: 40,
            icon: const Icon(Icons.skip_next),
            onPressed: available ? () => act(context, () => radio.call('media.next', {'source': source})) : null,
          ),
        ]),
      ]),
    );
  }

  Widget _radioCard(BuildContext context, Map<String, dynamic> r) {
    final radio = RadioScope.of(context);
    final theme = Theme.of(context);
    final band = '${r['band'] ?? 'fm'}';
    final min = asDouble(r['min'], 880), max = asDouble(r['max'], 1080);
    final value = (_dial ?? asDouble(r['value'], min)).clamp(min, max);
    final playing = r['playing'] == true;
    String label(double v) => band == 'am' ? '${(v * 10).round()}' : (v / 10).toStringAsFixed(1);
    final presets = asList(r['presets']);

    return SectionCard(
      child: Column(children: [
        Row(children: [
          SegmentedButton<String>(
            segments: const [ButtonSegment(value: 'fm', label: Text('FM')), ButtonSegment(value: 'am', label: Text('AM'))],
            selected: {band},
            onSelectionChanged: (s) => act(context, () => radio.call('radio.band', {'band': s.first})),
          ),
          const Spacer(),
          IconButton.filled(
            iconSize: 32,
            icon: Icon(playing ? Icons.stop : Icons.play_arrow),
            onPressed: r['available'] == true ? () => act(context, () => radio.call(playing ? 'radio.stop' : 'radio.play')) : null,
          ),
        ]),
        const SizedBox(height: 8),
        Row(mainAxisAlignment: MainAxisAlignment.center, crossAxisAlignment: CrossAxisAlignment.baseline, textBaseline: TextBaseline.alphabetic, children: [
          Text(label(value), style: theme.textTheme.displayMedium),
          const SizedBox(width: 8),
          Text('${r['unit'] ?? 'MHz'}', style: theme.textTheme.titleMedium),
        ]),
        Row(children: [
          IconButton(icon: const Icon(Icons.chevron_left, size: 36), onPressed: () => act(context, () => radio.call('radio.step', {'delta': -1}))),
          Expanded(
            child: Slider(
              value: value,
              min: min,
              max: max,
              divisions: (max - min).round().clamp(1, 1000),
              label: label(value),
              onChanged: (v) => setState(() => _dial = v),
              onChangeEnd: (v) {
                act(context, () => radio.call('radio.tune', {'value': v.round()}));
                setState(() => _dial = null);
              },
            ),
          ),
          IconButton(icon: const Icon(Icons.chevron_right, size: 36), onPressed: () => act(context, () => radio.call('radio.step', {'delta': 1}))),
        ]),
        const SizedBox(height: 8),
        Text('Presets: tap to tune, hold to save the current station', style: theme.textTheme.bodySmall),
        const SizedBox(height: 8),
        Wrap(spacing: 8, runSpacing: 8, alignment: WrapAlignment.center, children: [
          for (final p in presets)
            GestureDetector(
              onLongPress: () => act(context, () => radio.call('radio.save_preset', {'index': asInt(asMap(p)['index'])}),
                  done: 'Saved ${label(value)} as preset ${asInt(asMap(p)['index']) + 1}'),
              child: ChoiceChip(
                label: Text('${asMap(p)['label']}'.isEmpty ? '+' : '${asMap(p)['label']}'),
                selected: asInt(asMap(p)['value']) == asInt(r['value']) && asInt(asMap(p)['value']) > 0,
                onSelected: (_) {
                  final i = asInt(asMap(p)['index']);
                  if (asInt(asMap(p)['value']) > 0) {
                    act(context, () => radio.call('radio.preset', {'index': i}));
                  } else {
                    act(context, () => radio.call('radio.save_preset', {'index': i}), done: 'Saved ${label(value)} as preset ${i + 1}');
                  }
                },
              ),
            ),
        ]),
      ]),
    );
  }
}
