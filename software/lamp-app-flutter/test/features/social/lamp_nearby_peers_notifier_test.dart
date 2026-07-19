// parse round-trip + backward-compat for LampNearbyPeer, and notifier
// empty-read-keeps-last-good behavior.
//
// The lamp emits per-peer JSON via the `nearby` page-protocol section.
// We test:
//   - Round-trip with all known fields, both color shapes (legacy 4-int
//     list and current 8-hex "RRGGBBWW" string).
//   - Firmware missing rssi defaults to -127 (Far via proximityFromRssi).
//   - A legacy `proximity` key is ignored without error.
//   - Firmware missing lampId defaults to empty string.
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
  test('parses full peer JSON with legacy 4-int colors', () {
    final json = {
      'name': 'jacko',
      'lampId': 'AA:BB:CC:DD:EE:FF',
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
    expect(peer.lampId, 'AA:BB:CC:DD:EE:FF');
    expect(peer.rssi, -72);
    expect(peer.baseRgbw, [255, 100, 0, 0]);
    expect(peer.shadeRgbw, [0, 0, 255, 0]);
    expect(peer.viaBle, true);
    expect(peer.viaEspNow, true);
    expect(peer.lastSeenMs, 12345);
  });

  test('parses hex-string colors (current firmware shape)', () {
    final json = {
      'name': 'jacko',
      'base': 'FF640000',
      'shade': '0000FF00',
      'viaBle': true,
      'viaEspNow': true,
      'lastSeenMs': 12345,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.baseRgbw, [255, 100, 0, 0]);
    expect(peer.shadeRgbw, [0, 0, 255, 0]);
  });

  test('malformed color values fall back to black', () {
    expect(
      LampNearbyPeer.fromJson({'name': 'x', 'base': 'ZZ640000'}).baseRgbw,
      [0, 0, 0, 0],
    );
    expect(
      LampNearbyPeer.fromJson({'name': 'x', 'base': 'FF00'}).baseRgbw,
      [0, 0, 0, 0],
    );
    expect(
      LampNearbyPeer.fromJson({
        'name': 'x',
        'base': [255, 'nope', 0],
      }).baseRgbw,
      [255, 0, 0, 0],
    );
  });

  test('peer missing rssi defaults to -127 and derives Far', () {
    final json = {
      'name': 'jacko',
      'lampId': 'AA:BB:CC:DD:EE:FF',
      'base': [255, 100, 0, 0],
      'shade': [0, 0, 255, 0],
      'viaBle': true,
      'viaEspNow': false,
      'lastSeenMs': 12345,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.rssi, -127);
    expect(proximityFromRssi(peer.rssi), 2);
  });

  test('proximityFromRssi buckets match firmware tiers', () {
    expect(proximityFromRssi(-30), 0);
    expect(proximityFromRssi(-80), 0);
    expect(proximityFromRssi(-81), 1);
    expect(proximityFromRssi(-90), 1);
    expect(proximityFromRssi(-91), 2);
    expect(proximityFromRssi(-127), 2);
  });

  test('peer missing lampId defaults to empty string', () {
    // Older firmware may omit lampId. The model accepts that path
    // with lampId = ''.
    final json = {
      'name': 'oldlamp',
      'base': [0, 0, 0, 0],
      'shade': [0, 0, 0, 0],
      'viaBle': true,
      'viaEspNow': false,
      'lastSeenMs': 0,
    };
    final peer = LampNearbyPeer.fromJson(json);
    expect(peer.lampId, '');
    expect(peer.rssi, -127);
  });

  test('minimal JSON (just a name) parses with all defaults', () {
    final peer = LampNearbyPeer.fromJson({'name': 'minimal'});
    expect(peer.name, 'minimal');
    expect(peer.lampId, '');
    expect(peer.rssi, -127);
    expect(peer.baseRgbw, [0, 0, 0, 0]);
    expect(peer.shadeRgbw, [0, 0, 0, 0]);
  });

  test('zero-byte read after good read keeps last-good peer list', () async {
    final ble = InMemoryBleClient();
    const lampId = 'lamp1';
    await ble.connect(lampId);

    final goodBytes = Uint8List.fromList(utf8.encode(
      '[{"name":"alice","lampId":"AA:BB:CC:DD:EE:FF",'
      '"base":"00000000","shade":"00000000",'
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
      (_, _) {},
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

  test('non-List decode after good read keeps last-good peer list', () async {
    // A page-read race can pull another section's bytes (a `lamp` JSON
    // object) into the `nearby` read. The decode must throw, not surface an
    // empty list, so the last-good snapshot survives.
    final ble = InMemoryBleClient();
    const lampId = 'lamp1';
    await ble.connect(lampId);

    final goodBytes = Uint8List.fromList(utf8.encode(
      '[{"name":"alice","lampId":"AA:BB:CC:DD:EE:FF",'
      '"base":"00000000","shade":"00000000",'
      '"viaBle":true,"viaEspNow":false,"lastSeenMs":1}]',
    ));
    ble.seedSection(lampId, 'nearby', goodBytes);

    final container = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(container.dispose);
    final sub = container.listen(
      lampNearbyPeersNotifierProvider(lampId),
      (_, _) {},
    );
    addTearDown(sub.close);

    await container.read(lampNearbyPeersNotifierProvider(lampId).future);
    expect(
      container.read(lampNearbyPeersNotifierProvider(lampId)).value,
      hasLength(1),
    );

    // Replace with a `lamp`-shaped JSON object (a non-List decode).
    ble.seedSection(
      lampId,
      'nearby',
      Uint8List.fromList(utf8.encode('{"name":"alice","socialMode":1}')),
    );
    await Future.delayed(const Duration(milliseconds: 1100));

    final afterState =
        container.read(lampNearbyPeersNotifierProvider(lampId)).value!;
    expect(afterState, hasLength(1));
    expect(afterState.first.name, 'alice');
  });
}
