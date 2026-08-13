import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';

part 'dispositions_notifier.g.dart';

/// Per-peer social disposition for a given lamp.
///
/// Reads CHAR_SOCIAL_DISPOSITIONS once at build; subsequent edits update
/// an in-memory map AND schedule a debounced write back to the lamp (500
/// ms after the last edit). The full map is sent on every write: the
/// firmware-side characteristic replaces the entire state on each write.
///
/// Disposition values are 1..5 (salty, wary, neutral, fond, smitten).
/// Missing keys default to 3 (neutral) at the call site via `get`.
///
/// Keys are the peer's lamp-reported lampId (mesh MAC, colon-hex, from the
/// connected lamp's `nearby` section, `LampNearbyPeer.lampId`), NOT the
/// peer's user-set name or the phone's BLE remoteId. The firmware emits the
/// same address on both platforms, so keys are stable across Android and iOS.
@Riverpod(keepAlive: false, name: 'dispositionsProvider')
class Dispositions extends _$Dispositions {
  Timer? _flushTimer;
  Map<String, int> _local = const {};
  // Cache the BleClient at build() so the onDispose flush can still
  // dispatch the trailing write AFTER the provider has been torn down.
  // Without this, `_flush` calls `ref.read(bleClientProvider)` post-
  // dispose and throws. The throw is swallowed silently by the
  // try/catch below, and the user's last slider edit vanishes.
  late final BleClient _ble;

  @override
  Future<Map<String, int>> build(String lampId) async {
    _ble = ref.read(bleClientProvider);
    ref.onDispose(() {
      // If a debounced write is pending when the provider disposes
      // (user dragged the slider and switched tabs / backgrounded the
      // app within the 500ms window), flush it instead of dropping it.
      // Otherwise the local state diverges from the lamp and next visit
      // re-reads the OLD value and silently clobbers the user's edit.
      if (_flushTimer?.isActive ?? false) {
        _flushTimer!.cancel();
        unawaited(_flush());
      }
    });
    try {
      final bytes = await _ble.read(
        lampId,
        BleUuids.controlService,
        BleUuids.socialDispositions,
      );
      if (bytes.isEmpty) {
        _local = {};
        return _local;
      }
      final parsed = jsonDecode(utf8.decode(bytes));
      if (parsed is Map<String, dynamic>) {
        _local = {
          for (final entry in parsed.entries)
            entry.key: (entry.value as num).toInt().clamp(1, 5),
        };
      } else {
        _local = {};
      }
    } catch (_) {
      // Read can fail before auth; UI treats absence as "all neutral."
      _local = {};
    }
    return _local;
  }

  /// Disposition for `lampId`, defaulting to 3 (neutral) when unset.
  int get(String lampId) => _local[lampId] ?? 3;

  /// Set the disposition for `lampId`. Updates local state immediately
  /// (so the slider doesn't bounce) and schedules a debounced write
  /// to the lamp 500 ms after the last edit.
  void set(String lampId, int value) {
    final clamped = value.clamp(1, 5);
    if (lampId.isEmpty) return;
    final updated = Map<String, int>.from(_local);
    updated[lampId] = clamped;
    _local = updated;
    state = AsyncData(updated);
    _scheduleFlush();
  }

  void _scheduleFlush() {
    _flushTimer?.cancel();
    _flushTimer = Timer(const Duration(milliseconds: 500), _flush);
  }

  Future<void> _flush() async {
    final json = jsonEncode(_local);
    try {
      await _ble.write(
        lampId,
        BleUuids.controlService,
        BleUuids.socialDispositions,
        Uint8List.fromList(utf8.encode(json)),
      );
    } catch (_) {
      // Best-effort. If the write failed (disconnect, auth lost), the
      // lamp will keep its previous value. UI doesn't surface this to
      // the user; they can re-toggle when reconnected.
    }
  }
}
