import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/presentation/widgets/lamp_color_swatch.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_painted_lamps.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/drift_controls.dart';

import '../../_support/in_memory_ble_client.dart';

class _FakePeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-split-widgets';
const _wispMac = 'AA:BB:CC:DD:EE:FF';
const _claimedMac = '10:20:30:40:50:60';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

const _config =
    '{"char":"wispConfig","ledType":"GRBW","px":60,"brightness":80,'
    '"driftIntervalMs":90000,"driftFadePct":40,"shuffleSeed":7,"opSeq":0}';

/// Fixture: a Manual wisp with a two-colour palette claiming one lamp that
/// has no live paint yet, so the preview has only prediction to go on.
Future<ProviderContainer> _container({
  required String status,
  String config = '{}',
  int? lampFw,
}) async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await ble.connect(_devId);
  ble.seedSection(_devId, 'wispstatus', _b(status));
  ble.seedSection(_devId, 'wispconfig', _b(config));
  ble.seedSection(
    _devId,
    'wisppalette',
    Uint8List.fromList([255, 0, 0, 0, 0, 0, 255, 0]),
  );
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wispClaims,
    Uint8List.fromList([1, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60]),
  );
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      lampNearbyPeersNotifierProvider(_devId).overrideWith(() => _FakePeers()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c
      .read(inventoryNotifierProvider.notifier)
      .add(InventoryLamp(id: _devId, name: 'testlamp', fwVersion: lampFw));
  return c;
}

Future<void> _pump(
  WidgetTester tester,
  ProviderContainer c,
  Widget child,
) async {
  await tester.runAsync(() async {
    c.listen(wispNotifierProvider(_devId), (_, _) {});
    await c.read(wispNotifierProvider(_devId).future);
    await Future<void>.delayed(const Duration(milliseconds: 20));
  });
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(home: Scaffold(body: child)),
    ),
  );
  await tester.pump();
}

void main() {
  group('painted-lamps preview', () {
    // The status names a palette but not the wisp's shuffle seed, so a
    // prediction would use the placeholder seed and show the wrong colours.
    testWidgets('preview waits: no prediction while settings are unknown',
        (tester) async {
      final c = (await tester.runAsync(
        () => _container(
          status:
              '{"wispMac":"$_wispMac","configSplit":true,"source":"manual"}',
        ),
      ))!;
      addTearDown(c.dispose);
      await _pump(tester, c, const PaintedLampsList(lampId: _devId));

      expect(c.read(wispNotifierProvider(_devId)).value!.configKnown, isFalse);
      expect(find.textContaining(_claimedMac.substring(12)), findsOneWidget);
      expect(find.byType(LampColorSwatch), findsNothing);
    });

    testWidgets('the same fixture predicts once settings are known',
        (tester) async {
      final c = (await tester.runAsync(
        () => _container(
          status:
              '{"wispMac":"$_wispMac","configSplit":true,"source":"manual",'
              '"configLastSeenMs":100}',
          config: _config,
        ),
      ))!;
      addTearDown(c.dispose);
      await _pump(tester, c, const PaintedLampsList(lampId: _devId));

      expect(c.read(wispNotifierProvider(_devId)).value!.configKnown, isTrue);
      expect(find.byType(LampColorSwatch), findsNWidgets(2));
    });
  });

  group('settings placeholder', () {
    const status =
        '{"wispMac":"$_wispMac","configSplit":true,"source":"osc"}';
    const unknown = WispStatus(wispMac: _wispMac, configSplit: true);

    testWidgets('a lamp older than 1.2.8 asks to be updated', (tester) async {
      final c = (await tester.runAsync(
        () => _container(status: status, lampFw: 0x010207),
      ))!;
      addTearDown(c.dispose);
      await _pump(
        tester,
        c,
        const DriftControls(lampId: _devId, status: unknown),
      );

      expect(find.text("Update this lamp to see the wisp's settings"),
          findsOneWidget);
      expect(find.text('Reading settings from wisp…'), findsNothing);
    });

    testWidgets('a 1.2.8 lamp shows the loading line', (tester) async {
      final c = (await tester.runAsync(
        () => _container(status: status, lampFw: 0x010208),
      ))!;
      addTearDown(c.dispose);
      await _pump(
        tester,
        c,
        const DriftControls(lampId: _devId, status: unknown),
      );

      expect(find.text('Reading settings from wisp…'), findsOneWidget);
      expect(find.textContaining('Update this lamp'), findsNothing);
    });
  });
}
