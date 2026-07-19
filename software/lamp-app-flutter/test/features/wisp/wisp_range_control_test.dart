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
import 'package:lamp_app/features/wisp/presentation/widgets/wisp_range_control.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-range-test';

const _seedStatus = WispStatus(
  wispMac: 'AA:BB:CC:DD:EE:FF',
  rangeStep: 1,
);

Future<(ProviderContainer, InMemoryBleClient)> _makeContainer() async {
  SharedPreferences.setMockInitialValues({});
  final ble = InMemoryBleClient();
  await seedControlBle(ble, deviceId: _devId);
  await ble.connect(_devId);
  await ble.write(
    _devId,
    BleUuids.controlService,
    BleUuids.wispStatus,
    Uint8List.fromList(
      utf8.encode('{"wispMac":"AA:BB:CC:DD:EE:FF","range":1}'),
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
  return (c, ble);
}

Widget _wrap(ProviderContainer c, WispStatus status) =>
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: WispRangeControl(lampId: _devId, status: status),
          ),
        ),
      ),
    );

void main() {
  group('WispRangeControl', () {
    testWidgets('renders the seeded step name and reach text', (tester) async {
      final (c, _) = await _makeContainer();
      addTearDown(c.dispose);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      expect(find.text('Camp'), findsOneWidget);
      final slider =
          tester.widget<Slider>(find.byKey(const Key('wisp-range-slider')));
      expect(slider.value, 1.0);
      expect(find.byKey(const Key('wisp-range-info')), findsOneWidget);
    });

    testWidgets('releasing the slider at Wide writes setRange wispOp',
        (tester) async {
      final (c, ble) = await _makeContainer();
      addTearDown(c.dispose);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });

      await tester.pumpWidget(_wrap(c, _seedStatus));
      await tester.pump();

      await tester.drag(
          find.byKey(const Key('wisp-range-slider')), const Offset(600, 0));
      for (var i = 0; i < 10; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }

      final written = await ble.read(
        _devId,
        BleUuids.controlService,
        BleUuids.wispOp,
      );
      final decoded = jsonDecode(utf8.decode(written)) as Map<String, dynamic>;
      expect(decoded['op'], 'setRange');
      expect(decoded['range'], 3);
      expect(find.text('Wide'), findsWidgets);
    });
  });
}
