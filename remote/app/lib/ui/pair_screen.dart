import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../protocol/client.dart';
import '../radio_model.dart';
import 'common.dart';

/// First time with this radio: it shows a code, the phone sends it back.
class PairScreen extends StatefulWidget {
  const PairScreen({super.key});
  @override
  State<PairScreen> createState() => _PairScreenState();
}

class _PairScreenState extends State<PairScreen> {
  final _name = TextEditingController(text: 'My phone');
  final _code = TextEditingController();
  String? _error;
  bool _busy = false;

  Future<void> _request() async {
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      await RadioScope.read(context).requestPairing(_name.text.trim());
    } catch (e) {
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _confirm() async {
    final radio = RadioScope.read(context);
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      await radio.confirmPairing(_code.text.replaceAll(' ', ''));
    } on RemoteError catch (e) {
      setState(() => _error = e.message);
      // Too many wrong codes ends the pairing on the radio: ask for a new code
      if (e.message.contains('start again')) {
        radio.restartPairing();
        _code.clear();
      }
    } catch (e) {
      setState(() => _error = '$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final radio = RadioScope.of(context);
    final asking = radio.link == LinkState.pairing;
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text('Pair with ${radio.radioName}'),
        leading: IconButton(icon: const Icon(Icons.close), onPressed: radio.detach),
      ),
      body: ListView(padding: const EdgeInsets.all(24), children: [
        Icon(Icons.lock_outline, size: 56, color: theme.colorScheme.primary),
        const SizedBox(height: 16),
        Text(
          asking ? 'Enter the code shown on the radio' : 'This phone hasn\'t controlled this radio before',
          style: theme.textTheme.headlineSmall,
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 8),
        Text(
          asking
              ? 'It stays on the radio\'s screen for 2 minutes.'
              : 'The radio will show a 6-digit code to make sure it\'s yours. Over ${radio.transportKind}.',
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 24),
        if (!asking) ...[
          TextField(
            controller: _name,
            decoration: const InputDecoration(labelText: 'This phone\'s name (shown on the radio)', border: OutlineInputBorder()),
          ),
          const SizedBox(height: 16),
          FilledButton(onPressed: _busy ? null : _request, child: const Text('Show the code on the radio')),
        ] else ...[
          TextField(
            controller: _code,
            autofocus: true,
            keyboardType: TextInputType.number,
            textAlign: TextAlign.center,
            maxLength: 6,
            inputFormatters: [FilteringTextInputFormatter.digitsOnly],
            style: theme.textTheme.headlineMedium?.copyWith(letterSpacing: 8),
            decoration: const InputDecoration(border: OutlineInputBorder(), counterText: ''),
            onChanged: (v) {
              if (v.length == 6 && !_busy) _confirm();
            },
          ),
          const SizedBox(height: 16),
          FilledButton(onPressed: _busy ? null : _confirm, child: const Text('Pair')),
          TextButton(onPressed: _busy ? null : _request, child: const Text('Show a new code')),
        ],
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(top: 16),
            child: Text(_error!, style: TextStyle(color: theme.colorScheme.error), textAlign: TextAlign.center),
          ),
      ]),
    );
  }
}
