// lampId-keyed disposition cross-reference + lamp-emitted
// nearby peer list.
//
// Three scenarios:
// 1. Lamp emits a peer with lampId X + name N:
//    row label is N and slider reflects the stored disposition for X.
// 2. No peers in the lamp's nearby list:
//    row is not rendered (empty-state copy shows).
// 3. Same lampId re-emitted with new name (the rename case):
//    row label re-renders; disposition value unchanged.
//
// Test wiring:
// * `lampNearbyPeersNotifierProvider(lampId)` is overridden with
//   `_FakeLampNearbyPeers` (or `_MutableFakeLampNearbyPeers` for the
//   rename case). The production notifier installs a Timer.periodic
//   against BleClient.readSection — bypassing build() avoids the
//   pending-timer assertion at dispose and the BLE I/O entirely.
// * `bleClientProvider` is overridden with `InMemoryBleClient`; the
//   CHAR_SOCIAL_DISPOSITIONS read is pre-seeded so the real
//   `Dispositions` notifier (which keys on lampId) populates its
//   in-memory map from the seeded JSON — this is the cross-reference
//   path under test.
// * `controlNotifierProvider` is overridden with a synthetic
//   `_FakeControl` because `SocialScreen` gates its body on
//   `ctl.hasState`. Bypassing the full ControlNotifier (which would
//   need section seeds + inventory + auth wiring) keeps this focused on
//   the disposition cross-reference behavior.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/core/widgets/critter_icon.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/social/domain/social_mode.dart';
import 'package:lamp_app/features/social/presentation/social_screen.dart';
import 'package:shared_preferences/shared_preferences.dart';

// SocialScreen gates on controlNotifierProvider — it renders a spinner
// until ctl.hasState. Bypass the BLE-heavy ControlNotifier.build() by
// subclassing and returning a synthetic ControlState directly.
//
// _CapturingControl also overrides triggerGreet so double-tap tests can
// assert the lampId sent without a live BLE connection.
class _CapturingControl extends ControlNotifier {
  _CapturingControl(this._selfName);
  final String _selfName;
  final List<String> greetedAddrs = [];

  @override
  Future<ControlState> build(String deviceId) async => _fakeState(_selfName);

  @override
  Future<void> triggerGreet(String lampId) async {
    greetedAddrs.add(lampId);
  }
}

ControlState _fakeState(String selfName, {String? selfLampId}) => ControlState(
      lamp: LampSection(
        name: selfName,
        brightness: 100,
        advancedEnabled: false,
        webappEnabled: true,
        socialMode: SocialMode.ambivert,
        lampId: selfLampId,
      ),
      base: const BaseSection(
        px: 35,
        bpp: 4,
        byteOrder: 'GRBW',
        colors: [],
        knockout: {},
      ),
      shade: const ShadeSection(
        px: 38,
        bpp: 4,
        byteOrder: 'GRBW',
        colors: [],
      ),
      home: const HomeSection(
        ssid: '',
        brightness: 60,
        enabled: false,
        networkBound: false,
        socialDisabled: true,
        disabledExpressionTypes: ['glitchy'],
      ),
      expressions: const ExpressionsSection(expressions: []),
    );

class _FakeControl extends ControlNotifier {
  _FakeControl(this._selfName, {this.selfLampId});
  final String _selfName;
  final String? selfLampId;
  @override
  Future<ControlState> build(String deviceId) async =>
      _fakeState(_selfName, selfLampId: selfLampId);
}

/// Stub for lampNearbyPeersNotifierProvider that returns a
/// static list without doing any BLE polling. Bypasses the
/// Timer.periodic that the production notifier installs, so tests
/// don't trip the framework's `!timersPending` invariant on dispose.
class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  _FakeLampNearbyPeers(this._seed);
  final List<LampNearbyPeer> _seed;
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => _seed;
}

/// Same idea, but with a `setPeers()` helper for tests that need to
/// simulate the lamp emitting a fresh nearby-snapshot mid-test (rename
/// case). Still no polling, still no BLE I/O.
class _MutableFakeLampNearbyPeers extends LampNearbyPeersNotifier {
  _MutableFakeLampNearbyPeers(this._seed);
  List<LampNearbyPeer> _seed;
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => _seed;

  /// Test helper: seed a new peer list and emit it to listeners.
  void setPeers(List<LampNearbyPeer> peers) {
    _seed = peers;
    state = AsyncData(peers);
  }
}

// Seeds the CHAR_SOCIAL_DISPOSITIONS read on the InMemoryBleClient.
// Production `Dispositions.build()` does `_ble.read(...)` against this
// (deviceId, service, char) tuple; the in-memory client returns the
// seeded bytes verbatim, which the notifier jsonDecodes into its
// in-memory map. Requires the device to be connect()'d first — the
// in-memory client's `read` throws BleNotConnected otherwise.
Future<void> _seedDispositions(
  InMemoryBleClient ble,
  String deviceId,
  Map<String, int> dispositions,
) async {
  await ble.connect(deviceId);
  await ble.write(
    deviceId,
    BleUuids.controlService,
    BleUuids.socialDispositions,
    Uint8List.fromList(utf8.encode(jsonEncode(dispositions))),
  );
}

void main() {
  testWidgets('row label shows current peer name when lampId is known',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(
      ble,
      'floral-id',
      const {'AA:BB:CC:DD:EE:FF': 4},
    );
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id')
            .overrideWith(() => _FakeControl('floral')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              name: 'jacko',
              lampId: 'AA:BB:CC:DD:EE:FF',
              rssi: -72,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    // Pump twice so:
    // 1) the synthetic ControlNotifier future resolves
    // 2) the SocialScreen body rebuilds with ctl.hasState=true, at which
    //    point the dispositionsProvider mounts and starts its BLE read
    await tester.pump();
    await tester.pump();
    // Settle the disposition async build (it reads via BleClient).
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('jacko'), findsOneWidget);
    // Disposition value 4 → "fond" label.
    expect(find.text('fond'), findsOneWidget);
    // Proximity 0 → "Near".
    expect(find.text('Near'), findsOneWidget);
  });

  testWidgets('empty-state copy when no nearby lamps regardless of dispositions',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(
      ble,
      'floral-id',
      const {'AA:BB:CC:DD:EE:FF': 4},
    );
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id')
            .overrideWith(() => _FakeControl('floral')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const []),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.textContaining('No lamps nearby'), findsOneWidget);
    expect(find.text('jacko'), findsNothing);
  });

  testWidgets('same-named peer with different lampId is not self-filtered',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(ble, 'floral-id', const {});
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id').overrideWith(
            () => _FakeControl('floral', selfLampId: '11:22:33:44:55:66')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              name: 'floral',
              lampId: 'AA:BB:CC:DD:EE:FF',
              rssi: -72,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('floral'), findsOneWidget);
    expect(find.textContaining('No lamps nearby'), findsNothing);
  });

  testWidgets('peer whose lampId matches self is filtered out',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(ble, 'floral-id', const {});
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id').overrideWith(
            () => _FakeControl('floral', selfLampId: 'AA:BB:CC:DD:EE:FF')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              // Distinct name, same lampId, mixed case: still self.
              name: 'ghost',
              lampId: 'aa:bb:cc:dd:ee:ff',
              rssi: -72,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('ghost'), findsNothing);
    expect(find.textContaining('No lamps nearby'), findsOneWidget);
  });

  testWidgets('renamed peer label updates without disturbing disposition',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(
      ble,
      'floral-id',
      const {'AA:BB:CC:DD:EE:FF': 4},
    );
    late _MutableFakeLampNearbyPeers fakePeers;
    fakePeers = _MutableFakeLampNearbyPeers(const [
      LampNearbyPeer(
        name: 'jacko',
        lampId: 'AA:BB:CC:DD:EE:FF',
        rssi: -72,
      ),
    ]);
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id')
            .overrideWith(() => _FakeControl('floral')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => fakePeers,
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('jacko'), findsOneWidget);
    expect(find.text('fond'), findsOneWidget);

    // Rename simulation: same lampId, new name. Production parallel:
    // a fresh nearby JSON poll lands with the lamp emitting the new name
    // for the same peer.
    fakePeers.setPeers(const [
      LampNearbyPeer(
        name: 'jacko-test',
        lampId: 'AA:BB:CC:DD:EE:FF',
        rssi: -72,
      ),
    ]);
    await tester.pump();

    expect(find.text('jacko'), findsNothing);
    expect(find.text('jacko-test'), findsOneWidget);
    // Disposition (lampId-keyed) is unchanged because the lampId
    // didn't change: only the display name did.
    expect(find.text('fond'), findsOneWidget);
  });

  testWidgets('double-tap on peer row calls triggerGreet with correct lampId',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(
      ble,
      'floral-id',
      const {'AA:BB:CC:DD:EE:FF': 3},
    );
    late _CapturingControl capturer;
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id').overrideWith(() {
          capturer = _CapturingControl('floral');
          return capturer;
        }),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              name: 'jacko',
              lampId: 'AA:BB:CC:DD:EE:FF',
              rssi: -72,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    // Double-tap on the critter icon, the only tap target wired to
    // GestureDetector.onDoubleTap. Two taps within kDoubleTapTimeout
    // (300ms) trigger it.
    await tester.tap(find.byType(CritterIcon));
    await tester.pump(const Duration(milliseconds: 100));
    await tester.tap(find.byType(CritterIcon));
    await tester.pumpAndSettle();

    expect(capturer.greetedAddrs, ['AA:BB:CC:DD:EE:FF']);
  });

  testWidgets(
      'social join keys on lampId regardless of remoteId shape '
      '(iOS: id != lampId)', (tester) async {
    // Inventory's `id` is an opaque UUID unrelated to any mac (the iOS
    // remoteId shape) and its name differs from the peer's ('jacko-test'
    // vs 'jacko'), so the name-fallback rung can't produce a match either:
    // the row resolving to 'jacko-test' is only possible via the lampId join.
    SharedPreferences.setMockInitialValues({});
    final ble = InMemoryBleClient();
    await _seedDispositions(
      ble,
      'floral-id',
      const {'AA:BB:CC:DD:EE:FF': 4},
    );
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('floral-id')
            .overrideWith(() => _FakeControl('floral')),
        lampNearbyPeersNotifierProvider('floral-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              // Lamp emits old BLE-adv name (its BLE adv may lag the
              // rename update by a power cycle); inventory has the
              // fresh name from the rename dialog.
              name: 'jacko',
              lampId: 'AA:BB:CC:DD:EE:FF',
              rssi: -72,
            ),
          ]),
        ),
        // Seed inventory with jacko's lampId and the NEW name. The `id`
        // is an opaque iOS-shape UUID, never a mac, so the join can only
        // resolve via the mirrored `lampId`. Lowercase here vs the peer's
        // uppercase to prove the match is case-insensitive.
        inventoryNotifierProvider.overrideWith(
            () => _FakeInventory(const [
                  InventoryLamp(
                      id: 'ios-uuid-floral-peer',
                      lampId: 'aa:bb:cc:dd:ee:ff',
                      name: 'jacko-test'),
                ])),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'floral-id')),
      ),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    // Inventory wins: row label is jacko-test, not jacko.
    expect(find.text('jacko-test'), findsOneWidget);
    expect(find.text('jacko'), findsNothing);
    // Disposition (lampId-keyed) still resolves correctly.
    expect(find.text('fond'), findsOneWidget);
  });
}

class _FakeInventory extends InventoryNotifier {
  _FakeInventory(this._seed);
  final List<InventoryLamp> _seed;
  @override
  Future<List<InventoryLamp>> build() async => _seed;
}
