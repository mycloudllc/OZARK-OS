/// A link to the radio that carries whole JSON messages (PROTOCOL.md).
abstract class Transport {
  /// Each message the radio sends, as text.
  Stream<String> get messages;

  /// Completes when the link closes, for whatever reason.
  Future<void> get done;

  Future<void> send(String message);
  Future<void> close();

  /// "Wi-Fi" or "Bluetooth"
  String get kind;
}
