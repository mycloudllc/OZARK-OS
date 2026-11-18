import 'dart:convert';
import 'dart:typed_data';

/// Bluetooth LE framing: each message ends with a newline and travels in
/// pieces of at most MTU - 3 bytes.
class LineFramer {
  final _buffer = BytesBuilder(copy: false);
  final List<int> _pending = [];

  /// Adds a received piece; returns the messages it completed.
  List<String> add(List<int> piece) {
    final out = <String>[];
    for (final b in piece) {
      if (b == 0x0A) {
        final bytes = Uint8List.fromList(_pending);
        _pending.clear();
        final text = utf8.decode(bytes, allowMalformed: true).trim();
        if (text.isNotEmpty) out.add(text);
      } else {
        _pending.add(b);
      }
    }
    return out;
  }

  /// One message cut to size for the link.
  static List<Uint8List> split(String message, int mtu) {
    final data = utf8.encode('$message\n');
    final size = (mtu - 3).clamp(20, 512);
    return [
      for (var i = 0; i < data.length; i += size)
        Uint8List.fromList(data.sublist(i, i + size > data.length ? data.length : i + size)),
    ];
  }

  void reset() {
    _pending.clear();
    _buffer.clear();
  }
}
