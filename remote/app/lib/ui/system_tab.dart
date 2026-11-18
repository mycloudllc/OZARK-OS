import 'package:flutter/material.dart';

import 'common.dart';

class SystemTab extends StatefulWidget {
  const SystemTab({super.key});
  @override
  State<SystemTab> createState() => _SystemTabState();
}

class _SystemTabState extends State<SystemTab> {
  Map<String, dynamic> _info = {}, _net = {}, _update = {};
  List<dynamic> _phones = [];
  String _you = '';
  double? _brightness;
  bool _checking = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _load());
  }

  Future<void> _load() async {
    final radio = RadioScope.read(context);
    Future<Map<String, dynamic>> get(String m) async {
      try {
        return asMap(await radio.call(m));
      } catch (_) {
        return {};
      }
    }
    final info = await get('system.info');
    final net = await get('system.network');
    final update = await get('system.update.status');
    final phones = await get('clients.list');
    if (!mounted) return;
    setState(() {
      _info = info;
      _net = net;
      _update = update;
      _phones = asList(phones['clients']);
      _you = '${phones['you'] ?? ''}';
    });
  }

  Future<void> _hotspot() async {
    final name = TextEditingController(text: '${_net['hotspot_name'] ?? _net['hotspot.name'] ?? ''}');
    final pass = TextEditingController();
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Radio Wi-Fi'),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(controller: name, decoration: const InputDecoration(labelText: 'Name')),
          TextField(controller: pass, decoration: const InputDecoration(labelText: 'Password (8 or more characters)')),
          const SizedBox(height: 12),
          const Text('Phones on the radio\'s Wi-Fi, this one too, will need to join again.'),
        ]),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('Save')),
        ],
      ),
    );
    if (ok == true && mounted) {
      await act(context, () => RadioScope.read(context).call('system.hotspot.set', {'name': name.text, 'password': pass.text}),
          done: 'Saved');
    }
  }

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final theme = Theme.of(context);
    final dark = radio.dash['mode'] == 'dark';
    final netRows = _net.entries.where((e) => '${e.value}'.isNotEmpty && !e.key.contains('password') && !e.key.contains('key')).take(10);

    return RefreshIndicator(
      onRefresh: _load,
      child: ListView(children: [
        SectionCard(
          title: 'Screen',
          child: Column(children: [
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Dark mode'),
              value: dark,
              onChanged: (v) => act(context, () => radio.call('dash.theme', {'mode': v ? 'dark' : 'light'})),
            ),
            Row(children: [
              const Icon(Icons.brightness_6),
              Expanded(
                child: Slider(
                  value: (_brightness ?? asDouble(radio.dash['brightness'], 255)).clamp(0, 255),
                  max: 255,
                  onChanged: (v) => setState(() => _brightness = v),
                  onChangeEnd: (v) {
                    act(context, () => radio.call('dash.brightness', {'value': v.round()}));
                    setState(() => _brightness = null);
                  },
                ),
              ),
            ]),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text('Imperial units in this app'),
              subtitle: const Text('mph, °F, psi'),
              value: radio.imperial,
              onChanged: radio.setImperial,
            ),
          ]),
        ),
        SectionCard(
          title: 'Radio',
          child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            Text([radio.radioName, if ('${_info['version'] ?? ''}'.isNotEmpty) 'version ${_info['version']}'].join(' · '),
                style: theme.textTheme.bodyLarge),
            if (_info['uptime_s'] != null) Text('On for ${(asInt(_info['uptime_s']) / 3600).toStringAsFixed(1)} h'),
            const SizedBox(height: 8),
            Text('Software update: ${_update['available.version'] != null ? 'version ${_update['available.version']} available' : _update['state'] ?? 'unknown'}'),
            Row(children: [
              TextButton.icon(
                icon: _checking ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.system_update),
                label: const Text('Check for updates'),
                onPressed: _checking
                    ? null
                    : () async {
                        setState(() => _checking = true);
                        final r = await act(context, () => radio.call('system.update.check'));
                        if (mounted) {
                          setState(() {
                            _checking = false;
                            if (r != null) _update = asMap(r);
                          });
                        }
                      },
              ),
            ]),
            const Text('Updates are installed on the radio itself (Settings > Update).'),
          ]),
        ),
        SectionCard(
          title: 'Network',
          trailing: TextButton(onPressed: _hotspot, child: const Text('Change Wi-Fi')),
          child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            if (_net.isEmpty) const Text('Not available'),
            for (final e in netRows) Text('${e.key}: ${e.value}'),
          ]),
        ),
        SectionCard(
          title: 'Phones that control this radio',
          child: Column(children: [
            for (final p in _phones.map(asMap))
              ListTile(
                contentPadding: EdgeInsets.zero,
                leading: const Icon(Icons.smartphone),
                title: Text('${p['name']}${p['id'] == _you ? ' (this phone)' : ''}'),
                subtitle: Text(p['connected'] == true ? 'Connected' : 'Last used ${p['last_seen']}'),
                trailing: p['id'] == _you
                    ? null
                    : TextButton(
                        onPressed: () async {
                          await act(context, () => radio.call('clients.revoke', {'id': p['id']}));
                          _load();
                        },
                        child: const Text('Remove'),
                      ),
              ),
          ]),
        ),
        Padding(
          padding: const EdgeInsets.all(16),
          child: Wrap(spacing: 12, runSpacing: 8, children: [
            OutlinedButton.icon(icon: const Icon(Icons.link_off), label: const Text('Disconnect'), onPressed: radio.detach),
            OutlinedButton.icon(
              icon: const Icon(Icons.delete_outline),
              label: const Text('Forget this radio'),
              onPressed: () async {
                await act(context, () => radio.call('clients.revoke', {'id': _you}));
                await radio.forgetRadio();
              },
            ),
            OutlinedButton.icon(
              icon: const Icon(Icons.restart_alt),
              label: const Text('Restart radio'),
              onPressed: () async {
                final ok = await showDialog<bool>(
                  context: context,
                  builder: (c) => AlertDialog(
                    title: const Text('Restart the radio?'),
                    actions: [
                      TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
                      FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('Restart')),
                    ],
                  ),
                );
                if (ok == true && context.mounted) act(context, () => radio.call('system.reboot', {'confirm': true}));
              },
            ),
          ]),
        ),
      ]),
    );
  }
}
