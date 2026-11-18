import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/lamp_crypto.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/domain/wisp_config.dart';
import 'package:lamp_app/features/wisp/domain/wisp_source_mode.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart';

import '../../_support/in_memory_ble_client.dart';
import '../../_support/seed.dart';

class _FakePeers extends LampNearbyPeersNotifier {
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => const [];
}

const _devId = 'lamp-osc-mode';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _status({
  String source = 'osc',
  int seen = 100,
  bool live = false,
  String mac = _mac,
}) => jsonEncode({
  'wispMac': mac,
  'configSplit': true,
  'source': source,
  'sourceLive': live,
  'manualId': '',
  'configLastSeenMs': seen,
});

/// A `wispConfig` with [mode] and [slot] as given; null leaves the key out,
/// as a wisp that predates them sends.
String _config({
  Object? mode = 'straylight',
  Object? slot = 1,
  int brightness = 100,
  String ip = '192.168.1.42',
}) => jsonEncode({
  'char': 'wispConfig',
  'ledType': 'GRB',
  'px': 30,
  'brightness': brightness,
  'driftIntervalMs': 120000,
  'driftFadePct': 50,
  'shuffleSeed': 0,
  'opSeq': 0,
  'ip': ip,
  'swatches': false,
  'oscMode': ?mode,
  'oscSlot': ?slot,
});

List<Map<String, dynamic>> _oscOps(InMemoryBleClient ble) => [
  for (final w in ble.writesTo(_devId, BleUuids.wispOp))
    if (w.isNotEmpty && w[0] == 0x7B)
      if (jsonDecode(utf8.decode(w)) case final Map<String, dynamic> op
          when op['op'] == 'setOsc')
        op,
];

void _notify(InMemoryBleClient ble, String json) => ble.simulateNotify(
  _devId,
  BleUuids.controlService,
  BleUuids.wispStatus,
  _b(json),
);

void main() {
  group('OSC sender in the notifier', () {
    final defaultWindow = WispNotifier.debugConfigConfirmWindow;
    final defaultReadBack = WispNotifier.debugConfigReadBackDelay;
    setUp(() {
      SharedPreferences.setMockInitialValues({});
      WispNotifier.debugConfigConfirmWindow = const Duration(milliseconds: 300);
      WispNotifier.debugConfigReadBackDelay = const Duration(milliseconds: 40);
    });
    tearDown(() {
      WispNotifier.debugConfigConfirmWindow = defaultWindow;
      WispNotifier.debugConfigReadBackDelay = defaultReadBack;
    });

    Future<(InMemoryBleClient, ProviderContainer, WispNotifier)> open({
      String? config,
    }) async {
      final ble = InMemoryBleClient();
      await ble.connect(_devId);
      ble.seedSection(_devId, 'wispstatus', _b(_status()));
      ble.seedSection(_devId, 'wispconfig', _b(config ?? _config(slot: 2)));
      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      addTearDown(c.dispose);
      c.listen(wispNotifierProvider(_devId), (_, _) {});
      await c.read(wispNotifierProvider(_devId).future);
      await pumpEventQueue();
      return (ble, c, c.read(wispNotifierProvider(_devId).notifier));
    }

    WispStatus shown(ProviderContainer c) =>
        c.read(wispNotifierProvider(_devId)).value!;

    test('Maestro shows at once and confirms on the wisp config', () async {
      final (ble, c, n) = await open();
      expect(shown(c).oscMode, OscMode.straylight);

      final done = n.setOscMode(OscMode.maestro);
      await pumpEventQueue();
      expect(shown(c).oscMode, OscMode.maestro);
      expect(shown(c).oscSlot, 2, reason: 'the stored slot is kept');
      expect(_oscOps(ble), [
        {'char': 'wispOp', 'op': 'setOsc', 'mode': 'maestro'},
      ], reason: 'no slot: the wisp keeps its stored one');

      ble.seedSection(_devId, 'wispconfig', _b(_config(mode: 'maestro')));
      _notify(ble, _status(seen: 200));
      await done;

      expect(shown(c).oscMode, OscMode.maestro);
      expect(_oscOps(ble), hasLength(1), reason: 'confirmed, no resend');
    });

    test('rolls back and throws when the wisp never confirms', () async {
      final (ble, c, n) = await open();

      Object? error;
      await n.setOscMode(OscMode.maestro).catchError((Object e) => error = e);

      expect(error, isNotNull);
      expect(shown(c).oscMode, OscMode.straylight);
      expect(_oscOps(ble), hasLength(4), reason: 'resent each attempt');
    });

    test(
      'a config read from before the wisp applied it holds the switch',
      () async {
        final (ble, c, n) = await open();

        final done = n.setOscMode(OscMode.maestro);
        await pumpEventQueue();
        _notify(ble, _status(seen: 150));
        await pumpEventQueue();
        _notify(ble, _status(seen: 150));
        await pumpEventQueue();
        expect(
          shown(c).oscMode,
          OscMode.maestro,
          reason: 'stale config held off',
        );

        ble.seedSection(_devId, 'wispconfig', _b(_config(mode: 'maestro')));
        _notify(ble, _status(seen: 200));
        await done;

        // Once confirmed, the wisp's config rules again.
        ble.seedSection(_devId, 'wispconfig', _b(_config()));
        _notify(ble, _status(seen: 300));
        await pumpEventQueue();
        expect(shown(c).oscMode, OscMode.straylight);
      },
    );

    test('a newer pick stops the older one resending', () async {
      final (ble, c, n) = await open();

      final first = n.setOscMode(OscMode.maestro);
      await pumpEventQueue();
      final second = n.setOscMode(OscMode.straylight);
      await Future.wait([first, second]);

      expect(shown(c).oscMode, OscMode.straylight);
      expect(_oscOps(ble).where((op) => op['mode'] == 'maestro'), hasLength(1));
    });

    test('a slot sends the Maestro with it and confirms on that slot', () async {
      final (ble, c, n) = await open(config: _config(slot: 2));

      var confirmed = false;
      final done = n.setMaestroSlot(7).then((_) => confirmed = true);
      await pumpEventQueue();
      expect(shown(c).oscSlot, 7);
      expect(_oscOps(ble).single, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'maestro',
        'slot': 7,
      });

      // The Maestro landed with the old slot, beside another setting's change.
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'maestro', slot: 2, brightness: 40)),
      );
      _notify(ble, _status(seen: 200));
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(shown(c).brightness, 40, reason: 'that config was read');
      expect(shown(c).oscSlot, 7, reason: 'the pick still holds');
      expect(confirmed, isFalse, reason: 'only the slot confirms it');

      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'maestro', slot: 7)),
      );
      _notify(ble, _status(seen: 300));
      await done;
      expect(confirmed, isTrue);
      expect(shown(c).oscSlot, 7);
    });

    test('a slot the wisp never takes goes back to its slot', () async {
      final (ble, c, n) = await open(config: _config(mode: 'maestro', slot: 2));

      Object? error;
      await n.setMaestroSlot(7).catchError((Object e) => error = e);

      expect(error, isNotNull);
      expect(shown(c).oscSlot, 2);
      expect(_oscOps(ble), hasLength(4));
    });

    test('a slot still confirming rides along with a sender change', () async {
      final (ble, _, n) = await open(config: _config(mode: 'maestro', slot: 2));

      final first = n.setMaestroSlot(7).catchError((Object _) {});
      await pumpEventQueue();
      final second = n.setOscMode(OscMode.straylight).catchError((Object _) {});
      await pumpEventQueue();

      expect(_oscOps(ble).last, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'straylight',
        'slot': 7,
      });
      await Future.wait([first, second]);
    });

    test('a status from another wisp carries no pending pick', () async {
      final (ble, c, n) = await open();

      final done = n.setOscMode(OscMode.maestro).catchError((Object _) {});
      await pumpEventQueue();
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: null, slot: null)),
      );
      _notify(ble, _status(seen: 500, mac: '11:22:33:44:55:66'));
      await pumpEventQueue();

      expect(shown(c).wispMac, '11:22:33:44:55:66');
      expect(shown(c).oscMode, isNull);
      await done;
    });

    // The lamp relays every resend to the wisp it shows now.
    test('a pick stops resending once another wisp is shown', () async {
      final (ble, _, n) = await open();

      Object? error;
      final done = n
          .setOscMode(OscMode.maestro)
          .catchError((Object e) => error = e);
      await pumpEventQueue();
      _notify(ble, _status(seen: 500, mac: '11:22:33:44:55:66'));
      await done;

      expect(_oscOps(ble), hasLength(1));
      expect(
        error,
        isNotNull,
        reason: 'never confirmed by the wisp it went to',
      );
    });

    test('a pick stops resending once another wisp is heard, '
        'though a pending source change holds its status off', () async {
      final (ble, c, n) = await open();

      unawaited(n.setSource(WispSourceMode.manual).catchError((Object _) {}));
      await pumpEventQueue();
      final done = n.setOscMode(OscMode.maestro).catchError((Object _) {});
      await pumpEventQueue();
      _notify(ble, _status(seen: 500, mac: '11:22:33:44:55:66'));
      await done;

      expect(shown(c).wispMac, _mac, reason: 'the other wisp was held off');
      expect(_oscOps(ble), hasLength(1));
    });

    // The lamp's first status read lands before its auth, so build waits and
    // then takes the status a notify brought meanwhile.
    test('a wisp heard while build waits still stops a pick', () async {
      WispNotifier.debugConfigConfirmWindow = const Duration(seconds: 2);
      final ble = InMemoryBleClient();
      await ble.connect(_devId);
      await ble.write(
        _devId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        Uint8List(0),
      );
      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      addTearDown(c.dispose);
      c.listen(wispNotifierProvider(_devId), (_, _) {});
      final built = c.read(wispNotifierProvider(_devId).future);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      _notify(ble, _status());
      await pumpEventQueue();
      final n = c.read(wispNotifierProvider(_devId).notifier);

      unawaited(n.setSource(WispSourceMode.manual).catchError((Object _) {}));
      await pumpEventQueue();
      final done = n.setOscMode(OscMode.maestro).catchError((Object _) {});
      await pumpEventQueue();
      _notify(ble, _status(seen: 500, mac: '11:22:33:44:55:66'));
      await built;
      await done;

      expect(
        c.read(wispNotifierProvider(_devId)).value!.wispMac,
        _mac,
        reason: 'the other wisp was held off',
      );
      expect(_oscOps(ble), hasLength(1));
    });

    test('the same pick again while it confirms sends nothing more', () async {
      final (ble, _, n) = await open();

      final first = n.setMaestroSlot(7).catchError((Object _) {});
      await pumpEventQueue();
      await n.setMaestroSlot(7);
      await pumpEventQueue();

      expect(_oscOps(ble), hasLength(1));
      await first;
    });

    test('a brightness drag keeps the held sender and slot', () async {
      final (ble, c, n) = await open(config: _config(mode: 'maestro', slot: 3));

      n.setBrightness(40);
      _notify(ble, _status(seen: 100));
      await pumpEventQueue();

      expect(shown(c).brightness, 40);
      expect(shown(c).oscMode, OscMode.maestro);
      expect(shown(c).oscSlot, 3);
      n.flushBrightness();
      await pumpEventQueue();
    });

    test('sealed under the wisp password once one is set', () async {
      final (ble, _, n) = await open();
      await n.cachePassword(_mac, 'hunter2');

      final done = n.setOscMode(OscMode.maestro).catchError((Object _) {});
      await pumpEventQueue();

      final wire = ble.writesTo(_devId, BleUuids.wispOp).last;
      expect(wire[0], LampCrypto.magicCiphertext);
      final plain = await LampCrypto.decryptOpForTesting(
        wire,
        password: 'hunter2',
        saltUuid16: uuidSaltLE16(BleUuids.wispOp),
        charShortName: 'wispOp',
      );
      expect(jsonDecode(utf8.decode(plain)), {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'maestro',
      });
      await done;
    });
  });

  group('OSC sender on screen', () {
    Future<(InMemoryBleClient, ProviderContainer)> container({
      required String status,
      String? config,
    }) async {
      SharedPreferences.setMockInitialValues({});
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId);
      await ble.connect(_devId);
      ble.seedSection(_devId, 'wispstatus', _b(status));
      if (config != null) ble.seedSection(_devId, 'wispconfig', _b(config));
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          lampNearbyPeersNotifierProvider(
            _devId,
          ).overrideWith(() => _FakePeers()),
        ],
      );
      await c.read(inventoryNotifierProvider.future);
      await c
          .read(inventoryNotifierProvider.notifier)
          .add(const InventoryLamp(id: _devId, name: 'testlamp'));
      return (ble, c);
    }

    Future<void> pump(WidgetTester tester, ProviderContainer c) async {
      tester.view.physicalSize = const Size(800, 2400);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.runAsync(() async {
        await c.read(wispNotifierProvider(_devId).future);
      });
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: const MaterialApp(
            home: Scaffold(body: WispConfigScreen(lampId: _devId)),
          ),
        ),
      );
      for (var i = 0; i < 20; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
    }

    final modeSwitch = find.byKey(const Key('wisp-osc-mode'));
    final rolePicker = find.byKey(const Key('wisp-osc-role-picker'));
    final slotPicker = find.byKey(const Key('wisp-maestro-slot'));
    final sendTo = find.byKey(const Key('wisp-osc-send-to'));

    Set<OscMode> selected(WidgetTester tester) =>
        tester.widget<SegmentedButton<OscMode>>(modeSwitch).selected;

    testWidgets('a wisp whose config lacks oscMode shows only Straylight', (
      tester,
    ) async {
      final (_, c) = await container(
        status: _status(),
        config: _config(mode: null, slot: null),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(c.read(wispNotifierProvider(_devId)).value!.configKnown, isTrue);
      expect(modeSwitch, findsNothing);
      expect(find.text('Maestro'), findsNothing);
      expect(rolePicker, findsOneWidget);
      expect(find.text('Update this wisp to use Maestro.'), findsOneWidget);
      expect(find.textContaining('Waiting for Straylight'), findsOneWidget);
      expect(sendTo, findsOneWidget);
    });

    testWidgets(
      'Straylight: the switch, the role picker, and the send-to row',
      (tester) async {
        final (_, c) = await container(status: _status(), config: _config());
        addTearDown(c.dispose);
        await pump(tester, c);

        expect(selected(tester), {OscMode.straylight});
        expect(rolePicker, findsOneWidget);
        expect(find.byKey(const Key('wisp-osc-update-wisp')), findsNothing);
        expect(slotPicker, findsNothing);
        expect(find.textContaining('Waiting for Straylight'), findsOneWidget);
        expect(sendTo, findsOneWidget);
      },
    );

    testWidgets('Maestro: the slot, what to send, and the send-to row', (
      tester,
    ) async {
      final (_, c) = await container(
        status: _status(),
        config: _config(mode: 'maestro', slot: 3),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(selected(tester), {OscMode.maestro});
      expect(tester.widget<DropdownButton<int>>(slotPicker).value, 3);
      expect(
        find.text(
          'On the Maestro, send palette slot 3 to 192.168.1.42, port 9000.',
        ),
        findsOneWidget,
      );
      expect(find.textContaining('Waiting for the Maestro'), findsOneWidget);
      expect(rolePicker, findsNothing);
      expect(sendTo, findsOneWidget);
    });

    testWidgets('a live Maestro stream hides the waiting notice', (
      tester,
    ) async {
      final (_, c) = await container(
        status: _status(live: true),
        config: _config(mode: 'maestro', slot: 3),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(find.textContaining('Waiting for'), findsNothing);
    });

    testWidgets('an unknown sender selects nothing and asks for an update', (
      tester,
    ) async {
      final (_, c) = await container(
        status: _status(),
        config: _config(mode: 'aurora'),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(selected(tester), isEmpty);
      expect(find.byKey(const Key('wisp-osc-mode-unknown')), findsOneWidget);
      expect(rolePicker, findsNothing);
      expect(slotPicker, findsNothing);
    });

    testWidgets('nothing about the sender shows before the config is read', (
      tester,
    ) async {
      final (_, c) = await container(status: _status());
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(c.read(wispNotifierProvider(_devId)).value!.configKnown, isFalse);
      expect(modeSwitch, findsNothing);
      expect(rolePicker, findsNothing);
      expect(slotPicker, findsNothing);
      expect(find.textContaining('Waiting for'), findsNothing);
      expect(find.byKey(const Key('wisp-settings-pending')), findsWidgets);
    });

    testWidgets('tapping Maestro sends setOsc with no slot', (tester) async {
      final (ble, c) = await container(status: _status(), config: _config());
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.text('Maestro'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(_oscOps(ble), [
        {'char': 'wispOp', 'op': 'setOsc', 'mode': 'maestro'},
      ]);
      expect(slotPicker, findsOneWidget, reason: 'shown at once');
      // Run out the unconfirmed resends.
      await tester.pump(const Duration(seconds: 45));
      await tester.runAsync(() => pumpEventQueue());
    });

    testWidgets('picking a slot sends it', (tester) async {
      final (ble, c) = await container(
        status: _status(),
        config: _config(mode: 'maestro', slot: 3),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(slotPicker);
      await tester.pump();
      await tester.pump(const Duration(seconds: 1));
      await tester.tap(find.text('Slot 5').last);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(_oscOps(ble), [
        {'char': 'wispOp', 'op': 'setOsc', 'mode': 'maestro', 'slot': 5},
      ]);
      await tester.pump(const Duration(seconds: 45));
      await tester.runAsync(() => pumpEventQueue());
    });

    testWidgets('picking the slot already shown sends nothing', (tester) async {
      final (ble, c) = await container(
        status: _status(),
        config: _config(mode: 'maestro', slot: 3),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(slotPicker);
      await tester.pump();
      await tester.pump(const Duration(seconds: 1));
      await tester.tap(find.text('Slot 3').last);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(_oscOps(ble), isEmpty);
    });
  });

  group('OSC sender in the model', () {
    test('wispConfig oscMode and oscSlot parse; absent is null', () {
      final m = WispConfig.fromBytes(_b(_config(mode: 'maestro', slot: 10)))!;
      expect(m.oscMode, OscMode.maestro);
      expect(m.oscSlot, 10);
      final s = WispConfig.fromBytes(_b(_config()))!;
      expect(s.oscMode, OscMode.straylight);
      final old = WispConfig.fromBytes(_b(_config(mode: null, slot: null)))!;
      expect(old.oscMode, isNull);
      expect(old.oscSlot, isNull);
    });

    test('an unknown sender is kept as unknown; a slot off 1..10 is null', () {
      final c = WispConfig.fromBytes(_b(_config(mode: 'custom', slot: 11)))!;
      expect(c.oscMode, OscMode.unknown);
      expect(c.oscSlot, isNull);
      expect(WispConfig.fromBytes(_b(_config(slot: 0)))!.oscSlot, isNull);
    });

    test('a status merges them, compares on them, and copyWith keeps them', () {
      const base = WispStatus(wispMac: _mac, configSplit: true);
      WispStatus merged(Object? mode, Object? slot) => base.withConfig(
        WispConfig.fromBytes(_b(_config(mode: mode, slot: slot)))!,
      );
      final m3 = merged('maestro', 3);
      expect(m3.oscMode, OscMode.maestro);
      expect(m3.oscSlot, 3);
      expect(m3 == merged('straylight', 3), isFalse);
      expect(m3 == merged('maestro', 4), isFalse);
      expect(m3 == merged('maestro', 3), isTrue);
      final dragged = m3.copyWith(brightness: 10);
      expect((dragged.oscMode, dragged.oscSlot), (OscMode.maestro, 3));
      expect(
        m3.withConfig(WispConfig.fromBytes(_b(_config(mode: null)))!).oscMode,
        isNull,
      );
    });

    test('a held config keeps them through a brightness drag', () {
      final config = WispConfig.fromBytes(
        _b(_config(mode: 'maestro', slot: 3)),
      )!;
      final dragged = config.copyWith(brightness: 10);
      expect((dragged.oscMode, dragged.oscSlot), (OscMode.maestro, 3));
    });
  });
}
