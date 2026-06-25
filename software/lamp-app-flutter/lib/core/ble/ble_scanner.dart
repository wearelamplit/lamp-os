import 'dart:async';

import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;

/// BLE Manufacturer ID the firmware advertises (0xA455). The 6 bytes after
/// the company-ID prefix carry base RGB + shade RGB. See firmware:
/// software/lamp-os/src/components/network/bluetooth.cpp:83-93.
///
/// IMPORTANT: 0xA455 is NOT a registered Bluetooth SIG company identifier.
/// Any third-party device that picked the same 16-bit value would be
/// parsed as a lamp by [parseLampAdvertisement] unless we also verify
/// the payload length is one of the three known shapes (4, 6, or 7 bytes).
/// We do — see the length check below. A 3-byte adv from a random beacon
/// no longer slips through.
const _lampMfgId = 0xA455;

/// Valid lamp adv payload sizes (after the 2-byte company-ID prefix that
/// fbp strips). Anything outside this set is treated as non-lamp.
///
///   4 bytes [bR,bG,bB,meshFlag]: transitional v2 build that dropped
///                                 shade. Adv shadeRgb defaults to 0.
///   6 bytes [bR,bG,bB,sR,sG,sB]: v1 firmware (legacy). Real base + shade.
///   7 bytes [bR,bG,bB,sR,sG,sB,capabilities]: current firmware. Byte 6
///                                 is the capability bitfield.
const Set<int> _validLampPayloadLengths = {4, 6, 7};

/// Pure parser: turns a raw lamp adv (the mfg payload + envelope fields)
/// into a [BleAdvertisement], or null when the payload doesn't match a
/// lamp shape or the name is empty. Extracted into a top-level function
/// so it can be unit-tested without mocking flutter_blue_plus.
BleAdvertisement? parseLampAdvertisement({
  required Map<int, List<int>> manufacturerData,
  required String advName,
  required String platformName,
  required String remoteId,
  required int rssi,
}) {
  final mfg = manufacturerData[_lampMfgId];
  if (mfg == null) return null;
  if (!_validLampPayloadLengths.contains(mfg.length)) return null;
  // Bit 1 of byte 6 — "speaks the v0x03 mesh protocol". Byte-for-byte
  // identical to the prior `mfg[6] >= 2` check on existing fielded
  // firmware; forward-compatible for v3+ firmware that sets additional
  // bits. See software/lamp-os/.../bluetooth.cpp `kBleCapMeshProtocol`.
  const kBleCapMeshProtocol = 0x02;
  final hasShade = mfg.length >= 6;
  final isMesh =
      mfg.length >= 7 && (mfg[6] & kBleCapMeshProtocol) != 0;
  final name = advName.isNotEmpty ? advName : platformName;
  // Empty-name advs are noise — non-lamp devices that happen to collide
  // on the 16-bit mfg ID, lamps with a corrupted name chunk, or platform
  // glitches where the scan-response was lost. A nearby-list entry with
  // name='' renders as a blank, unselectable row in My Lamps.
  if (name.isEmpty) return null;
  return BleAdvertisement(
    id: remoteId,
    name: name,
    serviceUuids: const [],
    baseRgb: (mfg[0] << 16) | (mfg[1] << 8) | mfg[2],
    shadeRgb: hasShade
        ? (mfg[3] << 16) | (mfg[4] << 8) | mfg[5]
        : 0,
    rssi: rssi,
    isMesh: isMesh,
  );
}

class BleAdvertisement {
  const BleAdvertisement({
    required this.id,
    required this.name,
    required this.serviceUuids,
    required this.baseRgb,
    required this.shadeRgb,
    required this.rssi,
    this.isMesh = false,
  });

  final String id;
  final String name;
  final List<String> serviceUuids;
  /// Base color in 0xRRGGBB form, parsed from the lamp manufacturer data.
  final int baseRgb;
  /// Shade color in 0xRRGGBB form, parsed from the lamp manufacturer data.
  /// `0` for legacy 6-byte-payload v2 firmware (no shade in adv).
  final int shadeRgb;
  final int rssi;

  /// True iff this lamp's firmware advertises the version byte
  /// (mfg.length >= 7 && mfg[6] >= 2) — i.e. it speaks the app's
  /// mesh protocol and is fully app-controllable. v1 lamps and
  /// transitional pre-shade-restore v2 builds get `false` (the
  /// former because they're genuinely BT-only, the latter because
  /// they won't be on the network long).
  final bool isMesh;
}

abstract class BleScanner {
  /// Stream of scan results. Implementations must filter to lamp
  /// advertisements (manufacturer-data magic 0xA455) so callers don't see
  /// unrelated BLE traffic.
  Stream<BleAdvertisement> results();

  /// Begin scanning. Must be called once before [results] yields anything
  /// on the real driver. Idempotent — calling twice is a no-op.
  Future<void> start();

  /// Stop scanning (frees the radio).
  Future<void> stop();
}

class FbpBleScanner implements BleScanner {
  StreamSubscription<List<fbp.ScanResult>>? _sub;
  StreamSubscription<bool>? _scanningSub;
  final _ctrl = StreamController<BleAdvertisement>.broadcast();
  bool _running = false;

  @override
  Stream<BleAdvertisement> results() => _ctrl.stream;

  /// Power on the radio + start scanning. The fbp `startScan(timeout: 5min)`
  /// stops the scan after 5 minutes — without a restart loop the app would
  /// silently go blind. We listen to `isScanning` and re-issue startScan
  /// every time it flips to false while we still want to be scanning.
  /// Stop() flips `_running` to false so the restart loop quiesces.
  Future<void> _startNativeScan() async {
    await fbp.FlutterBluePlus.startScan(
      timeout: const Duration(minutes: 5),
      continuousUpdates: true,
    );
  }

  @override
  Future<void> start() async {
    if (_running) return;
    _running = true;
    _sub = fbp.FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        final ad = parseLampAdvertisement(
          manufacturerData: r.advertisementData.manufacturerData,
          advName: r.advertisementData.advName,
          platformName: r.device.platformName,
          remoteId: r.device.remoteId.str,
          rssi: r.rssi,
        );
        if (ad != null) _ctrl.add(ad);
      }
    });
    await _startNativeScan();
    // Watch for the platform-side scan ending (its own 5-min timeout, or
    // an unexpected radio stop) and re-issue startScan so users who leave
    // the app foregrounded longer than 5 minutes don't silently lose adv
    // visibility. The first emission can be the current state (true), so
    // gate on the running flag too.
    _scanningSub = fbp.FlutterBluePlus.isScanning.listen((isScanning) async {
      if (!_running) return;
      if (isScanning) return;
      // Best-effort restart. A failure here (e.g. permission revoked
      // mid-session) leaves _running true and the next isScanning event
      // re-tries.
      try {
        await _startNativeScan();
      } catch (_) {
        // intentionally swallowed — let the isScanning loop retry
      }
    });
  }

  @override
  Future<void> stop() async {
    if (!_running) return;
    _running = false;
    await fbp.FlutterBluePlus.stopScan();
    await _sub?.cancel();
    _sub = null;
    await _scanningSub?.cancel();
    _scanningSub = null;
  }
}

class FakeBleScanner implements BleScanner {
  final _ctrl = StreamController<BleAdvertisement>.broadcast();
  bool _started = false;

  @override
  Stream<BleAdvertisement> results() => _ctrl.stream;

  @override
  Future<void> start() async {
    _started = true;
  }

  @override
  Future<void> stop() async {
    _started = false;
  }

  void emit(BleAdvertisement ad) {
    if (!_started) {
      throw StateError('scanner not started');
    }
    _ctrl.add(ad);
  }
}
