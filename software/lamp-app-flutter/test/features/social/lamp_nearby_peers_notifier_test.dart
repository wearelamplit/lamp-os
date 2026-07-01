// parse round-trip + backward-compat for LampNearbyPeer, and notifier
// empty-read-keeps-last-good behavior.
//
// The lamp emits per-peer JSON via buildNearbyLampsJson. We test:
//   - Round-trip with all known fields.
//   - Firmware missing rssi/proximity defaults to -127/Far (safe-display fallback).
//   - Firmware missing bdAddr defaults to empty string.
//   - Empty / malformed inputs don't throw.
//   - A zero-byte read after a good read keeps the last-good peer list.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import '../../_support/in_memory_ble_client.dart';

void main() {
  test('parses full peer JSON', () {
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

  test('peer missing rssi/proximity defaults to -127/Far', () {
    // Firmware that emits bdAddr but no rssi or proximity defaults to
    // -127 RSSI and Far (2) — safe-display fallback.
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

  test('peer missing bdAddr defaults to empty string', () {
    // Older firmware may omit bdAddr. The model accepts that path
    // with bdAddr = ''.
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

  test('zero-byte read after good read keeps last-good peer list', () async {
    final ble = InMemoryBleClient();
    const lampId = 'lamp1';
    await ble.connect(lampId);

    final goodBytes = Uint8List.fromList(utf8.encode(
      '[{"name":"alice","bdAddr":"AA:BB:CC:DD:EE:FF",'
      '"base":[0,0,0,0],"shade":[0,0,0,0],'
      '"viaBle":true,"viaEspNow":false,"lastSeenMs":1}]',
    ));
    ble.seedSection(lampId, 'nearby', goodBytes);

    final container = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(container.dispose);

    // Keep the provider alive for the duration of the test (keepAlive: false
    // means it can be GC'd if nothing is subscribed).
    final sub = container.listen(
      lampNearbyPeersNotifierProvider(lampId),
      (_, __) {},
    );
    addTearDown(sub.close);

    // Wait for initial build — good data populates state and _lastGood.
    await container.read(lampNearbyPeersNotifierProvider(lampId).future);
    final goodState =
        container.read(lampNearbyPeersNotifierProvider(lampId)).value!;
    expect(goodState, hasLength(1));
    expect(goodState.first.name, 'alice');

    // Replace section with 0 bytes (transient glitch — empty list serialises
    // as "[]", not as 0 bytes, so 0 bytes is unambiguously transient).
    ble.seedSection(lampId, 'nearby', Uint8List(0));

    // Let one poll tick fire.
    await Future.delayed(const Duration(milliseconds: 1100));

    // State must still carry the last-good list.
    final afterState =
        container.read(lampNearbyPeersNotifierProvider(lampId)).value!;
    expect(afterState, hasLength(1));
    expect(afterState.first.name, 'alice');
  });
}
