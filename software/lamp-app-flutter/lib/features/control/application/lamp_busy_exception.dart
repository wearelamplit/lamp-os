/// Thrown when a connect or reconnect keeps reading a config section back as
/// busy, because the lamp is too low on memory to send it. Every lamp screen
/// shows it as a low-on-memory error with a retry (`LampErrorPage`).
class LampBusyException implements Exception {
  const LampBusyException();

  @override
  String toString() => 'LampBusyException';
}
