import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import 'dev_mode.dart';

part 'advanced_session.g.dart';

/// Session-only "advanced mode" flag per lamp: whether the user has unlocked
/// advanced UI in the current connection session. Distinct from the
/// app-global persistent `devModeProvider`; this is per-lamp, session-scoped,
/// and resets to false when the session ends (via
/// `ControlNotifier._onConnectionChange(false)`). Gates visibility only.
@Riverpod(keepAlive: true, name: 'advancedSessionProvider')
class AdvancedSession extends _$AdvancedSession {
  @override
  bool build(String lampId) => false;

  void enable() => state = true;
  void disable() => state = false;
  void toggle() => state = !state;
}

/// True when the session-unlock is active OR app-global dev mode is on.
/// Prefer this for read-only "is advanced UI showing?" checks;
/// `advancedSessionProvider` stays the authority for `.enable()`/`.disable()`.
final effectiveAdvancedProvider = Provider.family<bool, String>((ref, lampId) {
  if (ref.watch(advancedSessionProvider(lampId))) return true;
  return ref.watch(devModeOnProvider);
});
