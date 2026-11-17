import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/lamp_crypto.dart';
import '../../../core/ble/uuids.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../domain/wifi_state.dart';

part 'wifi_notifier.g.dart';

/// Owns the live `wifiOp` write surface and the `wifiState` notify stream
/// for a single lamp. Seeded by a read on build(); notifies thereafter.
///
/// Per-lamp via the `deviceId` family arg.
@Riverpod(name: 'wifiNotifierProvider')
class WifiNotifier extends _$WifiNotifier {
  StreamSubscription<Uint8List>? _sub;
  late String _deviceId;

  @override
  Future<WifiState> build(String deviceId) async {
    _deviceId = deviceId;
    final ble = ref.read(bleClientProvider);
    _subscribeState(ble);
    // The lamp is unbonded, so its notify subscription dies with the link
    // and has to be renewed on every reconnect.
    var linkUp = true;
    final connSub = ble.watchConnected(deviceId).listen((connected) {
      if (!connected) {
        linkUp = false;
        return;
      }
      if (linkUp) return;
      linkUp = true;
      _subscribeState(ble);
      unawaited(_rereadState(ble));
    });
    ref.onDispose(() {
      connSub.cancel();
      _sub?.cancel();
    });
    // Legacy lamps predate the wifiState characteristic; the read throws
    // BleNotFound. Degrade to an empty state so the network picker renders
    // its "tap refresh" prompt instead of erroring the whole surface.
    try {
      final bytes = await ble.read(
          deviceId, BleUuids.controlService, BleUuids.wifiState);
      return _parse(bytes);
    } catch (e) {
      debugPrint('[wifi_notifier] wifiState read failed for $deviceId: $e');
      return const WifiState();
    }
  }

  /// Reads the state again once the stream's first frame arrives, which a
  /// client that replays its cached value emits only after the notify is
  /// enabled, so a change landing before the enable is not lost.
  void _subscribeState(BleClient ble) {
    _sub?.cancel();
    var awaitingFirstFrame = true;
    _sub = ble
        .subscribe(_deviceId, BleUuids.controlService, BleUuids.wifiState)
        .listen(
      (bytes) {
        final parsed = _onNotify(bytes);
        if (awaitingFirstFrame || !parsed) {
          awaitingFirstFrame = false;
          unawaited(_rereadState(ble));
        }
      },
      onError: (Object e) =>
          debugPrint('[wifi_notifier] wifiState stream error: $e'),
    );
  }

  /// The lamp notifies only on a change, so a change made while the link
  /// was down is recovered by reading. The read on the reconnect edge can
  /// run before the renewed notify is enabled; the first-frame read in
  /// [_subscribeState] covers a change landing between the two.
  Future<void> _rereadState(BleClient ble) async {
    final WifiState? next;
    try {
      final bytes = await ble.read(
          _deviceId, BleUuids.controlService, BleUuids.wifiState);
      next = bytes.isEmpty ? null : _parse(bytes);
    } catch (e) {
      debugPrint('[wifi_notifier] wifiState re-read failed for $_deviceId: $e');
      return;
    }
    if (next != null && ref.mounted) _apply(next);
  }

  /// Applies a notified frame. Returns false when the frame does not parse,
  /// such as a scan list cut off at the notify size, so the caller reads the
  /// full value instead.
  bool _onNotify(Uint8List bytes) {
    // flutter_blue_plus replays an empty cached value when a subscription
    // starts after a reconnect.
    if (bytes.isEmpty) return true;
    final WifiState next;
    try {
      next = _parse(bytes);
    } catch (e) {
      debugPrint('[wifi_notifier] wifiState notify failed to parse for '
          '$_deviceId: $e');
      return false;
    }
    _apply(next);
    return true;
  }

  void _apply(WifiState next) {
    final cur = state.value ?? const WifiState();
    // Notifies for state transitions (connecting to connected to failed)
    // typically omit scanResults; preserve the last-known scan list across
    // those transitions so the UI doesn't blink the network list empty.
    state = AsyncData(next.copyWith(
      scanResults:
          next.scanResults.isEmpty ? cur.scanResults : next.scanResults,
    ));
  }

  /// A malformed frame throws a [FormatException] without the frame's
  /// source, which carries nearby SSIDs, so callers can log it as is.
  WifiState _parse(Uint8List bytes) {
    if (bytes.isEmpty) return const WifiState();
    final Object? json;
    try {
      json = jsonDecode(utf8.decode(bytes));
    } on FormatException catch (e) {
      throw FormatException(e.message, null, e.offset);
    }
    return WifiState.fromJson(json as Map<String, dynamic>);
  }

  Future<void> scan() => _writeOp({'op': 'scan'});

  // SSID operations live on controlNotifier (setHomeSsid), which writes
  // immediately via writeSettingsBlob (reboot:false). No "live" firmware
  // effect to preview for an SSID (home-mode detection runs off the saved
  // value), so no wifiOp channel is needed for setHomeSsid/forget.

  Future<void> _writeOp(Map<String, dynamic> op) async {
    final ble = ref.read(bleClientProvider);
    final inv = await ref.read(inventoryNotifierProvider.future);
    final lamp = inv.firstWhere(
      (l) => l.id == _deviceId,
      orElse: () => throw StateError(
          'WifiNotifier._writeOp: lamp $_deviceId not in inventory'),
    );
    final password = lamp.controlPassword ?? '';
    final bytes = password.isEmpty
        ? LampCrypto.wrapPlaintext(op)
        : await LampCrypto.encryptOp(
            op: op,
            password: password,
            saltUuid16: uuidSaltLE16(BleUuids.wifiOp),
            charShortName: 'wifiOp',
          );
    await ble.write(
        _deviceId, BleUuids.controlService, BleUuids.wifiOp, bytes);
  }
}
