import 'package:collection/collection.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../inventory/application/inventory_notifier.dart';

part 'advanced_session.g.dart';

/// Session-only "advanced mode" flag per lamp: whether the user has unlocked
/// advanced UI in the current connection session. Distinct from the
/// firmware-persisted `LampSettings.advancedEnabled`; this is app-side, and
/// resets to false when the session ends (via
/// `ControlNotifier._onConnectionChange(false)`). Gates visibility only; the
/// actual feature state lives in lamp config and persists independently.
@Riverpod(keepAlive: true, name: 'advancedSessionProvider')
class AdvancedSession extends _$AdvancedSession {
  @override
  bool build(String lampId) => false;

  void enable() => state = true;
  void disable() => state = false;
  void toggle() => state = !state;
}

/// True when the session-unlock is active OR the lamp's persisted `devMode`
/// is set. Prefer this for read-only "is advanced UI showing?" checks;
/// `advancedSessionProvider` stays the authority for `.enable()`/`.disable()`.
/// `devMode` reads from `inventoryNotifierProvider`, NOT `controlNotifier`,
/// whose `build()` opens a GATT connection per consumer (a connect storm for
/// list-view callers). The inventory mirror defaults false until first connect.
final effectiveAdvancedProvider = Provider.family<bool, String>((ref, lampId) {
  if (ref.watch(advancedSessionProvider(lampId))) return true;
  final devMode = ref.watch(
    inventoryNotifierProvider.select(
      (async) =>
          async.value?.firstWhereOrNull((l) => l.id == lampId)?.devMode ??
          false,
    ),
  );
  return devMode;
});
