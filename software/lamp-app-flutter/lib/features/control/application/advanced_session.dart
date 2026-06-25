import 'package:collection/collection.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../inventory/application/inventory_notifier.dart';

part 'advanced_session.g.dart';

/// Session-only "advanced mode" flag per lamp. Holds whether the user has
/// unlocked advanced UI in the current connection session.
///
/// Distinct from the firmware-persisted `LampSettings.advancedEnabled`
/// which the lamp itself stores in NVS — this provider is purely
/// app-side state, scoped to the current BLE connection session, and
/// resets to `false` whenever that session ends (handled by
/// `ControlNotifier._onConnectionChange(false)`).
///
/// Gates visibility of advanced UI like the expression cascade controls
/// and the Setup Hub's Advanced LED setup row. The actual feature state
/// (cascade params, LED config) lives in the lamp config and persists
/// across sessions independently of this flag — this only controls
/// whether the controls are visible.
@Riverpod(keepAlive: true, name: 'advancedSessionProvider')
class AdvancedSession extends _$AdvancedSession {
  @override
  bool build(String lampId) => false;

  void enable() => state = true;
  void disable() => state = false;
  void toggle() => state = !state;
}

/// Returns true when EITHER the session-unlock is active OR the lamp
/// has the persisted `devMode` flag set. Consumers should prefer this
/// over `advancedSessionProvider` for read-only "is the user seeing
/// advanced UI?" checks. The raw `advancedSessionProvider` stays the
/// authority for `.enable()` / `.disable()` mutations (the session flag
/// itself doesn't move when devMode is set; we just short-circuit reads).
///
/// `devMode` is read from `inventoryNotifierProvider`, NOT from
/// `controlNotifierProvider`. `controlNotifier.build()` opens a GATT
/// connection on instantiation, so reading devMode through it would
/// force a connect for every consumer — fatal for list-view callers
/// (picker tiles, etc.). The inventory mirror is populated by
/// `controlNotifier` post-section-read; until first connection the
/// flag is whatever was persisted last (default `false` for new lamps).
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
