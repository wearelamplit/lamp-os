// Phase D: parse round-trip + backward-compat for LampNearbyPeer.
//
// The lamp emits per-peer JSON via buildNearbyLampsJson. We test:
//   - Round-trip with full Phase-D fields.
//   - Pre-Phase-D firmware (missing rssi/proximity) defaults to
//     -127 / 2 (Far) — safe-display fallback.
//   - Pre-Phase-C firmware (missing bdAddr too) defaults to empty.
//   - Empty / malformed inputs don't throw.

import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';

void main() {
  test('parses full Phase-D peer JSON', () {
    final json = {
      'name': 'jacko',
      'bdAddr': 'AA:BB:CC:DD:EE:FF',
      'rssi': -72,
      'proximity': 0,
      'base': [255, 100, 0, 0],
      'shade': [0, 0, 255, 0],
      'viaBle': true,
      'viaEspNow': true,
      'lastSeenMs': 12345,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.name, 'jacko');
    expect(peer.bdAddr, 'AA:BB:CC:DD:EE:FF');
    expect(peer.rssi, -72);
    expect(peer.proximity, 0);
    expect(peer.baseRgbw, [255, 100, 0, 0]);
    expect(peer.shadeRgbw, [0, 0, 255, 0]);
    expect(peer.viaBle, true);
    expect(peer.viaEspNow, true);
    expect(peer.lastSeenMs, 12345);
  });

  test('pre-Phase-D peer (missing rssi/proximity) defaults to -127/Far',
      () {
    // Firmware between Phase C and Phase D emits bdAddr but no rssi
    // or proximity. The model defaults to -127 RSSI and Far (2)
    // proximity — safe-display fallback.
    final json = {
      'name': 'jacko',
      'bdAddr': 'AA:BB:CC:DD:EE:FF',
      'base': [255, 100, 0, 0],
      'shade': [0, 0, 255, 0],
      'viaBle': true,
      'viaEspNow': false,
      'lastSeenMs': 12345,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.rssi, -127);
    expect(peer.proximity, 2);
  });

  test('pre-Phase-C peer (missing bdAddr) defaults to empty string', () {
    // Pre-Phase-C firmware emitted no bdAddr at all. The model
    // accepts that path with bdAddr = ''.
    final json = {
      'name': 'oldlamp',
      'base': [0, 0, 0, 0],
      'shade': [0, 0, 0, 0],
      'viaBle': true,
      'viaEspNow': false,
      'lastSeenMs': 0,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.bdAddr, '');
    expect(peer.rssi, -127);
    expect(peer.proximity, 2);
  });

  test('minimal JSON (just a name) parses with all defaults', () {
    // Defensive: a malformed firmware emit with only the name field
    // should yield a peer with safe defaults, not throw.
    final peer = LampNearbyPeer.fromJson({'name': 'minimal'});
    expect(peer.name, 'minimal');
    expect(peer.bdAddr, '');
    expect(peer.rssi, -127);
    expect(peer.proximity, 2);
    expect(peer.baseRgbw, [0, 0, 0, 0]);
    expect(peer.shadeRgbw, [0, 0, 0, 0]);
  });
}
