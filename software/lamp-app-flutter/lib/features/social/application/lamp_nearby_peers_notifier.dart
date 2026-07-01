import 'dart:async';
import 'dart:convert';

import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../domain/lamp_nearby_peer.dart';

part 'lamp_nearby_peers_notifier.g.dart';

/// Polling interval for the lamp's nearby JSON. The page-protocol
/// section has no NOTIFY path, so polling is required. 1 Hz balances
/// freshness on rename/disposition changes against battery cost.
const _pollInterval = Duration(seconds: 1);

/// Per-lamp view of peers the connected lamp can hear. Reads the
/// `nearby` page-protocol section from CHAR_NEARBY_LAMPS at 1 Hz.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects — only surfaces AsyncError after sustained
/// failure (currently: never auto-fails; the social screen will
/// render "No lamps nearby" once the connection is restored and a
/// real empty response lands).
///
/// Family-keyed by lampId (deviceId in the BleClient sense). Each
/// lamp connection runs its own polling loop, scoped to the screen
/// that's looking at that lamp.
@Riverpod(keepAlive: false, name: 'lampNearbyPeersNotifierProvider')
class LampNearbyPeersNotifier extends _$LampNearbyPeersNotifier {
  Timer? _pollTimer;
  StreamSubscription<bool>? _connSub;
  late final BleClient _ble;
  bool _connected = false;
  // Last successful decode — preserved across transient errors so the
  // UI doesn't blip back to an empty list on a single failed poll.
  List<LampNearbyPeer> _lastGood = const [];

  @override
  Future<List<LampNearbyPeer>> build(String lampId) async {
    _ble = ref.read(bleClientProvider);
    ref.onDispose(() {
      _pollTimer?.cancel();
      _pollTimer = null;
      _connSub?.cancel();
      _connSub = null;
    });

    // Keep the last good snapshot while disconnected so the UI doesn't
    // churn during reconnect.
    _connSub = _ble.watchConnected(lampId).listen((isConnected) {
      _connected = isConnected;
      if (isConnected) {
        _startPolling();
      } else {
        _pollTimer?.cancel();
        _pollTimer = null;
      }
    });

    // Best-effort initial read; throws when disconnected and the
    // watchConnected stream handles resumption.
    try {
      final peers = await _readOnce();
      _lastGood = peers;
      return peers;
    } catch (_) {
      return _lastGood; // empty on cold start
    }
  }

  void _startPolling() {
    _pollTimer?.cancel();
    _pollTimer = Timer.periodic(_pollInterval, (_) => _poll());
  }

  Future<void> _poll() async {
    if (!_connected) return;
    try {
      final peers = await _readOnce();
      _lastGood = peers;
      // Only emit a state update if the contents actually changed —
      // avoids gratuitous rebuilds when nothing's different.
      if (!_listsEqual(state.value, peers)) {
        state = AsyncData(peers);
      }
    } catch (_) {
      // Keep last good snapshot on transient error; next tick retries.
    }
  }

  Future<List<LampNearbyPeer>> _readOnce() async {
    final bytes = await _ble.readSection(lampId, 'nearby');
    // A zero-byte read is a transient glitch (a genuinely empty list
    // serializes as "[]"). Throw so callers keep the last good snapshot
    // instead of flashing an empty list and dropping every peer's name.
    if (bytes.isEmpty) {
      throw const FormatException('empty nearby section read');
    }
    final decoded = jsonDecode(utf8.decode(bytes));
    if (decoded is! List) return const [];
    return decoded
        .whereType<Map<String, dynamic>>()
        .map(LampNearbyPeer.fromJson)
        .toList(growable: false);
  }

  static bool _listsEqual(List<LampNearbyPeer>? a, List<LampNearbyPeer>? b) {
    if (identical(a, b)) return true;
    if (a == null || b == null) return false;
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
