import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/manual_palette_draft.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakePeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-draft-test';
const _mac = 'AA:BB:CC:DD:EE:FF';
const _saved = [
  LampColor(r: 200, g: 10, b: 30, w: 0),
  LampColor(r: 5, g: 90, b: 160, w: 0),
  LampColor(r: 40, g: 40, b: 0, w: 70),
];

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// What the wisp config screen shows in the manual palette editor across the
/// draft's seeding and the user's own empty save.
void main() {
  late InMemoryBleClient ble;

  Future<ProviderContainer> makeContainer() async {
    SharedPreferences.setMockInitialValues({});
    ble = InMemoryBleClient();
    await seedControlBle(ble, deviceId: _devId);
    ble.seedSection(
      _devId,
      'wisppalette',
      Uint8List.fromList([
        for (final c in _saved) ...[c.r, c.g, c.b, c.w],
      ]),
    );
    await ble.connect(_devId);
    await ble.write(
      _devId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      _b('{"wispMac":"$_mac","source":"manual",'
          '"paletteIdPrefix":"${WispRepository.paletteIdHash(_saved)}"}'),
    );
    final c = ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        lampNearbyPeersNotifierProvider(_devId)
            .overrideWith(() => _FakePeers()),
      ],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
          id: _devId,
          name: 'testlamp',
          controlPassword: 'secret',
        ));
    return c;
  }

  Future<void> pumpScreen(WidgetTester tester, ProviderContainer c) async {
    await tester.runAsync(() async {
      await c.read(wispNotifierProvider(_devId).future);
    });
    await tester.pumpWidget(UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(
        home: Scaffold(body: WispConfigScreen(lampId: _devId)),
      ),
    ));
    for (var i = 0; i < 20; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
  }

  // The notifier was built under runAsync, so its BLE stream callbacks run on
  // the real event loop. Drain that, then pump frames to rebuild the screen.
  Future<void> settle(WidgetTester tester) async {
    for (var i = 0; i < 5; i++) {
      await tester.runAsync(pumpEventQueue);
      await tester.pump(const Duration(milliseconds: 16));
    }
  }

  testWidgets('first open of a Manual palette seeds the editor clean',
      (tester) async {
    final c = await makeContainer();
    await pumpScreen(tester, c);

    final n = c.read(wispNotifierProvider(_devId).notifier);
    expect(c.read(manualPaletteDraftProvider(_devId)), _saved);
    expect(n.manualPaletteDirty, isFalse);
  });

  testWidgets('an empty save keeps the editor empty through its confirm',
      (tester) async {
    final c = await makeContainer();
    await pumpScreen(tester, c);
    final n = c.read(wispNotifierProvider(_devId).notifier);
    expect(c.read(manualPaletteDraftProvider(_devId)), _saved);

    n.setManualPaletteDraft(const []);
    Object? err;
    final save = n.setManualPalette().catchError((Object e) => err = e);
    // Frames before the wisp answers rebuild the screen around the pending
    // save.
    await settle(tester);
    expect(c.read(manualPaletteDraftProvider(_devId)), isEmpty,
        reason: 'a pending empty save is not refilled');

    ble.seedSection(_devId, 'wisppalette', Uint8List(0));
    ble.simulateNotify(_devId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","source":"manual"}'));
    await settle(tester);
    await save;

    expect(err, isNull);
    expect(n.savedManualPalette, isEmpty);
    expect(c.read(manualPaletteDraftProvider(_devId)), isEmpty);
    expect(n.manualPaletteDirty, isFalse);
    expect(n.manualPaletteWriteFailed, isFalse);
  });

  testWidgets('a palette read during an empty save leaves the editor empty',
      (tester) async {
    const moved = [
      LampColor(r: 1, g: 250, b: 2, w: 0),
      LampColor(r: 3, g: 4, b: 240, w: 9),
    ];
    final c = await makeContainer();
    await pumpScreen(tester, c);
    final n = c.read(wispNotifierProvider(_devId).notifier);

    n.setManualPaletteDraft(const []);
    final save = n.setManualPalette().catchError((Object _) {});
    await settle(tester);

    ble.seedSection(
      _devId,
      'wisppalette',
      Uint8List.fromList([
        for (final c in moved) ...[c.r, c.g, c.b, c.w],
      ]),
    );
    ble.simulateNotify(_devId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","source":"manual",'
            '"paletteIdPrefix":"${WispRepository.paletteIdHash(moved)}"}'));
    await settle(tester);
    expect(n.savedManualPalette, moved, reason: 'the read landed');
    expect(c.read(manualPaletteDraftProvider(_devId)), isEmpty);

    ble.seedSection(_devId, 'wisppalette', Uint8List(0));
    ble.simulateNotify(_devId, BleUuids.controlService, BleUuids.wispStatus,
        _b('{"wispMac":"$_mac","source":"manual"}'));
    await settle(tester);
    await save;

    expect(c.read(manualPaletteDraftProvider(_devId)), isEmpty);
    expect(n.manualPaletteDirty, isFalse);
  });
}
