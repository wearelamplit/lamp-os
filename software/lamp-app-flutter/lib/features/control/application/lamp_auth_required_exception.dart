/// Thrown from [ControlNotifier.build] when CHAR_LAMP_SECTION reads back
/// empty bytes — the firmware's auth gate is rejecting the stored (or
/// missing) password. The control surface catches this and shows the
/// connect-time password prompt instead of the generic error view.
class LampAuthRequiredException implements Exception {
  const LampAuthRequiredException();

  @override
  String toString() => 'LampAuthRequiredException';
}
