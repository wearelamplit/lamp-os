import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../domain/lamp_nearby_peer.dart';

part 'lamp_nearby_peers_notifier.g.dart';

/// Fallback poll cadence for the lamp's nearby JSON. The primary path is a
/// push: stateNotify carries a `nearbyRev` that bumps on any app-visible
/// roster change, and a bump triggers an immediate re-read. This slow poll
/// only backstops a missed notify (e.g. a legacy lamp that predates
/// nearbyRev, or a dropped frame).
const _pollInterval = Duration(seconds: 20);

/// Per-lamp view of peers the connected lamp can hear. Reads the `nearby`
/// page-protocol section, re-reading when stateNotify's `nearbyRev` bumps
/// (push) and on a slow fallback poll.
///
/// Empty list while loading. Keeps the last-good snapshot on parse
/// errors and disconnects, only surfaces AsyncError after sustained
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
  StreamSubscription<Uint8List>? _notifySub;
  late final BleClient _ble;
  bool _connected = false;
  // Last nearbyRev seen on stateNotify. A change means the connected lamp's
  // roster gained/lost/renamed a peer (or a color/ota field moved); re-read.
  // null until the first frame seeds the baseline.
  int? _lastRev;
  // Last successful decode. Preserved across transient errors so the
  // UI doesn't blip back to an empty list on a single failed poll.
  List<LampNearbyPeer> _lastGood = const [];
  bool _polling = false;

  @override
  Future<List<LampNearbyPeer>> build(String lampId) async {
    _ble = ref.read(bleClientProvider);
    ref.onDispose(() {
      _pollTimer?.cancel();
      _pollTimer = null;
      _connSub?.cancel();
      _connSub = null;
      _notifySub?.cancel();
      _notifySub = null;
    });

    // Keep the last good snapshot while disconnected so the UI doesn't
    // churn during reconnect.
    _connSub = _ble.watchConnected(lampId).listen((isConnected) {
      _connected = isConnected;
      if (isConnected) {
        _startPolling();
        _startNotify();
      } else {
        _pollTimer?.cancel();
        _pollTimer = null;
        _notifySub?.cancel();
        _notifySub = null;
        _lastRev = null;
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

  // Immediate tick before the periodic timer catches a roster change that
  // happened while disconnected: subscribe replays the stale pre-disconnect
  // frame, so _onStateNotify reseeds _lastRev without triggering a re-read.
  void _startPolling() {
    _pollTimer?.cancel();
    _poll();
    _pollTimer = Timer.periodic(_pollInterval, (_) => _poll());
  }

  // subscribe (not subscribeNotifyOnly) replays the characteristic's last
  // value on listen, so a mid-session connect seeds _lastRev without waiting
  // for the next push.
  void _startNotify() {
    _notifySub?.cancel();
    _notifySub = _ble
        .subscribe(lampId, BleUuids.controlService, BleUuids.stateNotify)
        .listen(_onStateNotify, onError: (_) {});
  }

  void _onStateNotify(Uint8List frame) {
    if (frame.isEmpty) return;
    final int? rev;
    try {
      final decoded = jsonDecode(utf8.decode(frame));
      rev = decoded is Map ? decoded['nearbyRev'] as int? : null;
    } catch (_) {
      return;
    }
    if (rev == null) return;
    if (_lastRev == null) {
      _lastRev = rev;
      return;
    }
    if (rev != _lastRev) {
      _lastRev = rev;
      _poll();
    }
  }

  Future<void> _poll() async {
    if (!_connected || _polling) return;
    _polling = true;
    try {
      final peers = await _readOnce();
      _lastGood = peers;
      // Only emit a state update if the contents actually changed;
      // avoids gratuitous rebuilds when nothing's different.
      if (!_listsEqual(state.value, peers)) {
        state = AsyncData(peers);
      }
    } catch (_) {
      // Keep last good snapshot on transient error; next tick retries.
    } finally {
      _polling = false;
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
    // A non-List decode means the read pulled another section's bytes (a
    // `lamp` JSON object slipping into an interleaved `nearby` read). Throw
    // so the last-good snapshot survives instead of blanking every peer.
    if (decoded is! List) {
      throw const FormatException('nearby section decoded to non-List');
    }
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
