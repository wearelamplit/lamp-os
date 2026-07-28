import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/inventory/application/active_lamp_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/nearby/presentation/mesh_lamps_screen.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/social/domain/social_mode.dart';

ControlState _fakeState(
  String selfName, {
  int otaState = 0,
  String? otaSendingTo,
}) =>
    ControlState(
      lamp: LampSection(
        name: selfName,
        brightness: 100,
        advancedEnabled: false,
        webappEnabled: true,
        socialMode: SocialMode.ambivert,
        otaState: otaState,
        otaSendingTo: otaSendingTo,
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
  _FakeControl(this._selfName, {this._otaState = 0, this._otaSendingTo});
  final String _selfName;
  final int _otaState;
  final String? _otaSendingTo;
  @override
  Future<ControlState> build(String deviceId) async => _fakeState(
        _selfName,
        otaState: _otaState,
        otaSendingTo: _otaSendingTo,
      );
}

class _FakeInventory extends InventoryNotifier {
  _FakeInventory(this._seed);
  final List<InventoryLamp> _seed;
  @override
  Future<List<InventoryLamp>> build() async => _seed;
}

/// Stub for lampNearbyPeersNotifierProvider that returns a static list
/// without BLE polling. See test/features/social/social_screen_test.dart.
class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  _FakeLampNearbyPeers(this._seed);
  final List<LampNearbyPeer> _seed;
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => _seed;
}

class _FakeActiveLamp extends ActiveLampNotifier {
  _FakeActiveLamp(this._id);
  final String _id;
  @override
  Future<String?> build() async => _id;
}

void main() {
  testWidgets('groups peers into one Mesh section plus BT-only, no Near/Far',
      (tester) async {
    const lampId = 'floral-id';
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(InMemoryBleClient()),
        activeLampNotifierProvider.overrideWith(() => _FakeActiveLamp(lampId)),
        controlNotifierProvider(lampId).overrideWith(() => _FakeControl('floral')),
        lampNearbyPeersNotifierProvider(lampId).overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              name: 'meshpeer',
              lampId: 'AA:AA:AA:AA:AA:AA',
              rssi: -60,
              viaEspNow: true,
              viaBle: false,
              near: false,
            ),
            LampNearbyPeer(
              name: 'btpeer',
              lampId: 'BB:BB:BB:BB:BB:BB',
              rssi: -70,
              viaEspNow: false,
              viaBle: true,
              near: true,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(home: MeshLampsScreen()),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    // SectionHeader uppercases its label (see section_header_test.dart).
    expect(find.text('MESH'), findsOneWidget);
    expect(find.text('BT-ONLY'), findsOneWidget);
    expect(find.text('NEAR'), findsNothing);
    expect(find.text('FAR'), findsNothing);
    expect(find.text('meshpeer'), findsOneWidget);
    expect(find.text('btpeer'), findsOneWidget);
  });

  testWidgets('self row renders send pill with resolved receiver name '
      'when this lamp is the OTA source', (tester) async {
    const lampId = 'floral-id';
    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(InMemoryBleClient()),
        activeLampNotifierProvider.overrideWith(() => _FakeActiveLamp(lampId)),
        controlNotifierProvider(lampId).overrideWith(
          () => _FakeControl(
            'floral',
            otaState: 1,
            otaSendingTo: 'AA:AA:AA:AA:AA:AA',
          ),
        ),
        lampNearbyPeersNotifierProvider(lampId)
            .overrideWith(() => _FakeLampNearbyPeers(const [])),
        inventoryNotifierProvider.overrideWith(
          () => _FakeInventory(const [
            InventoryLamp(
                id: lampId, lampId: 'FF:FF:FF:FF:FF:FF', name: 'floral'),
            InventoryLamp(
                id: 'gramp-id',
                lampId: 'AA:AA:AA:AA:AA:AA',
                name: 'gramp'),
          ]),
        ),
      ],
      child: const MaterialApp(home: MeshLampsScreen()),
    ));
    await tester.pump();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('→ gramp'), findsOneWidget);
  });
}
