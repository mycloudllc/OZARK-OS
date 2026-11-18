import 'package:flutter/material.dart';

import 'common.dart';

class VehicleTab extends StatelessWidget {
  const VehicleTab({super.key});

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final theme = Theme.of(context);
    final status = asMap(radio.vehicle['status']);
    final obd = asMap(status['obd']);
    final values = asMap(radio.vehicle['values']);
    final tires = asMap(radio.vehicle['tires']);
    final dtcs = asMap(radio.vehicle['dtcs']);
    final imp = radio.imperial;

    String temp(Object? c) => imp ? '${(asDouble(c) * 9 / 5 + 32).round()} °F' : '${asDouble(c).round()} °C';
    final gauges = <(String, String, IconData)>[
      if (values.containsKey('speed_kmh'))
        ('Speed', imp ? '${(asDouble(values['speed_kmh']) / 1.609344).round()} mph' : '${asDouble(values['speed_kmh']).round()} km/h', Icons.speed),
      if (values.containsKey('rpm')) ('Engine', '${asDouble(values['rpm']).round()} rpm', Icons.settings_suggest),
      if (values.containsKey('coolant_c')) ('Coolant', temp(values['coolant_c']), Icons.thermostat),
      if (values.containsKey('oil_c')) ('Oil', temp(values['oil_c']), Icons.oil_barrel),
      if (values.containsKey('intake_c')) ('Intake', temp(values['intake_c']), Icons.air),
      if (values.containsKey('ambient_c')) ('Outside', temp(values['ambient_c']), Icons.wb_sunny_outlined),
      if (values.containsKey('voltage_v')) ('Battery', '${asDouble(values['voltage_v']).toStringAsFixed(1)} V', Icons.battery_charging_full),
      if (values.containsKey('fuel_pct')) ('Fuel', '${asDouble(values['fuel_pct']).round()} %', Icons.local_gas_station),
      if (values.containsKey('load_pct')) ('Load', '${asDouble(values['load_pct']).round()} %', Icons.fitness_center),
      if (values.containsKey('throttle_pct')) ('Throttle', '${asDouble(values['throttle_pct']).round()} %', Icons.unfold_more),
    ];

    final codes = [
      for (final kind in ['stored', 'pending', 'permanent'])
        for (final c in asList(dtcs[kind])) (kind, asMap(c)),
    ];

    return ListView(children: [
      SectionCard(
        title: 'OBD-II',
        child: Row(children: [
          Icon(obd['state'] == 'connected' ? Icons.check_circle : Icons.info_outline,
              color: obd['state'] == 'connected' ? Colors.green : theme.colorScheme.onSurfaceVariant),
          const SizedBox(width: 12),
          Expanded(child: Text('${obd['message'] ?? (status.isEmpty ? 'The vehicle service isn\'t running' : '')}')),
        ]),
      ),
      if (gauges.isNotEmpty)
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: GridView.count(
            crossAxisCount: 2,
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            childAspectRatio: 2.2,
            children: [
              for (final g in gauges)
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(12),
                    child: Row(children: [
                      Icon(g.$3, color: theme.colorScheme.primary),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Column(crossAxisAlignment: CrossAxisAlignment.start, mainAxisAlignment: MainAxisAlignment.center, children: [
                          Text(g.$1, style: theme.textTheme.labelMedium),
                          FittedBox(child: Text(g.$2, style: theme.textTheme.titleLarge)),
                        ]),
                      ),
                    ]),
                  ),
                ),
            ],
          ),
        ),
      SectionCard(
        title: 'Tires',
        child: tires.isEmpty
            ? const Text('No tire sensors reporting. Set them up in Settings > Bluetooth on the radio.')
            : Column(children: [
                for (final e in tires.entries)
                  () {
                    final t = asMap(e.value);
                    final kpa = asDouble(t['pressure_kpa']);
                    final warn = t['low'] == true || t['hot'] == true || t['alarm'] == true;
                    return ListTile(
                      dense: true,
                      leading: Icon(warn ? Icons.warning_amber : Icons.tire_repair, color: warn ? theme.colorScheme.error : null),
                      title: Text(e.key.replaceAll('_', ' ').replaceFirstMapped(RegExp(r'^.'), (m) => m[0]!.toUpperCase())),
                      subtitle: Text([if (t['low'] == true) 'low', if (t['hot'] == true) 'hot', if (t['alarm'] == true) 'alarm'].join(', ')),
                      trailing: Text('${imp ? (kpa / 6.89476).toStringAsFixed(1) : kpa.round()} ${imp ? 'psi' : 'kPa'}  ${temp(t['temp_c'])}'),
                    );
                  }(),
              ]),
      ),
      SectionCard(
        title: 'Trouble codes',
        trailing: Row(mainAxisSize: MainAxisSize.min, children: [
          TextButton(onPressed: () => act(context, () => radio.call('vehicle.dtcs.read'), done: 'Reading codes…'), child: const Text('Read')),
          TextButton(
            onPressed: () async {
              final ok = await showDialog<bool>(
                context: context,
                builder: (c) => AlertDialog(
                  title: const Text('Clear trouble codes?'),
                  content: const Text('This turns the check-engine light off and erases stored and pending codes. The engine should be off, ignition on.'),
                  actions: [
                    TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
                    FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('Clear')),
                  ],
                ),
              );
              if (ok == true && context.mounted) act(context, () => radio.call('vehicle.dtcs.clear'));
            },
            child: const Text('Clear'),
          ),
        ]),
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          if (radio.dtcsCleared.isNotEmpty) Text('${radio.dtcsCleared['message'] ?? ''}'),
          if (dtcs.isEmpty) const Text('Not read yet.'),
          if (dtcs.isNotEmpty && codes.isEmpty) Text(dtcs['ok'] == false ? '${dtcs['message']}' : 'No trouble codes.'),
          for (final (kind, c) in codes)
            ListTile(
              dense: true,
              leading: Text('${c['code']}', style: theme.textTheme.titleMedium),
              title: Text('${c['description'] ?? ''}'),
              subtitle: Text(kind),
            ),
        ]),
      ),
    ]);
  }
}
