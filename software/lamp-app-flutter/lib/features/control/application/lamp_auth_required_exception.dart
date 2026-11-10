/// Thrown when a connect or reconnect reads the `lamp` section back as
/// empty bytes. The firmware's auth gate is rejecting the stored (or
/// missing) password. The control surface catches this and shows the
/// connect-time password prompt instead of the generic error view.
class LampAuthRequiredException implements Exception {
  const LampAuthRequiredException();

  @override
  String toString() => 'LampAuthRequiredException';
}
