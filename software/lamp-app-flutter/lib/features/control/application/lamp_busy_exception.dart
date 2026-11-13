/// Thrown when a connect or reconnect keeps reading a config section back as
/// busy: the lamp is too low on memory to send it. The control surface shows
/// a low-on-memory error with a retry.
class LampBusyException implements Exception {
  const LampBusyException();

  @override
  String toString() => 'LampBusyException';
}
