import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_led_config.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-led-test';

const _seedStatus = WispStatus(
  wispMac: 'AA:BB:CC:DD:EE:FF',
  ledType: 'GRBW',
  pixelCount: 45,
);

Future<ProviderContainer> _makeContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId);
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    Uint8List.fromList(
      utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","ledType":"GRBW","px":45}'),
    ),
  );
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      lampNearbyPeersNotifierProvider(_devId)
          .overrideWith(() => _FakeLampNearbyPeers()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
        id: _devId,
        name: 'testlamp',
        controlPassword: 'secret',
      ));
  return c;
}

Widget _wrap(ProviderContainer c, WispStatus status) =>
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: WispLedConfig(lampId: _devId, status: status),
          ),
        ),
      ),
    );

void main() {
  group('WispLedConfig', () {
    testWidgets('renders with correct initial format selection', (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      final picker =
          tester.widget<SegmentedButton<String>>(find.byType(SegmentedButton<String>));
      expect(picker.selected, contains('GRBW'));
    });

    testWidgets('pixel count row shows initial value', (tester) async {
      final c = await _makeContainer();
      addTearDown(c.dispose);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      expect(
        find.descendant(
          of: find.byKey(const Key('wisp-led-count-row')),
          matching: find.text('45 LEDs'),
        ),
        findsOneWidget,
      );
    });

    testWidgets('tapping a format segment writes setLedStrip wispOp',
        (tester) async {
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId);
      await ble.connect(_devId);
      await ble.write(
        _devId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(
          utf8.encode(
              '{"wispMac":"AA:BB:CC:DD:EE:FF","ledType":"GRBW","px":45}'),
        ),
      );
      SharedPreferences.setMockInitialValues({});
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          lampNearbyPeersNotifierProvider(_devId)
              .overrideWith(() => _FakeLampNearbyPeers()),
        ],
      );
      addTearDown(c.dispose);
      await c.read(inventoryNotifierProvider.future);
      await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
            id: _devId,
            name: 'testlamp',
            controlPassword: 'secret',
          ));

      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      await tester.tap(find.text('BGR'));
      for (var i = 0; i < 10; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }

      final written = await ble.read(
        _devId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded = jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['op'], 'setLedStrip');
      expect(decoded['ledType'], 'BGR');
    });

    testWidgets('submitting a new pixel count writes setLedStrip wispOp',
        (tester) async {
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId);
      await ble.connect(_devId);
      await ble.write(
        _devId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List.fromList(
          utf8.encode(
              '{"wispMac":"AA:BB:CC:DD:EE:FF","ledType":"GRBW","px":45}'),
        ),
      );
      SharedPreferences.setMockInitialValues({});
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          lampNearbyPeersNotifierProvider(_devId)
              .overrideWith(() => _FakeLampNearbyPeers()),
        ],
      );
      addTearDown(c.dispose);
      await c.read(inventoryNotifierProvider.future);
      await c.read(inventoryNotifierProvider.notifier).add(const InventoryLamp(
            id: _devId,
            name: 'testlamp',
            controlPassword: 'secret',
          ));

      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      await tester.tap(find.byKey(const Key('wisp-led-count-row')));
      await tester.pumpAndSettle();

      await tester.enterText(find.byType(TextField), '60');
      await tester.tap(find.text('Save'));
      for (var i = 0; i < 10; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }

      final written = await ble.read(
        _devId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded = jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['op'], 'setLedStrip');
      expect(decoded['pixelCount'], 60);
    });
  });
}
