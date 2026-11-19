import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
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

/// Holds `pollStatus` writes while [holdPollStatus] is set, so the screen's
/// open-time `pollConfig`, which waits on it, stays out of a count.
class _HoldBle extends InMemoryBleClient {
  Completer<void>? holdPollStatus;

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    await super.write(
      d,
      s,
      c,
      v,
      withoutResponse: withoutResponse,
      allowLongWrite: allowLongWrite,
    );
    if (utf8.decode(v, allowMalformed: true).contains('pollStatus')) {
      await holdPollStatus?.future;
    }
  }
}

const _devId = 'lamp-osc-send-to';
const _mac = 'AA:BB:CC:DD:EE:FF';
const _noAddress = 'Connect to a network to receive OSC.';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _status({
  bool wifi = false,
  String ethIp = '',
  int seen = 100,
  int lastSeenMs = 1000,
}) =>
    jsonEncode({
      'wispMac': _mac,
      'configSplit': true,
      'source': 'osc',
      'lastSeenMs': lastSeenMs,
      'wifiConnected': wifi,
      if (ethIp.isNotEmpty) 'ethIp': ethIp,
      'configLastSeenMs': seen,
    });

/// A `wispConfig` with [ip] as given; null leaves the key out, as a wisp that
/// predates it sends.
String _config({String? ip}) => jsonEncode({
  'char': 'wispConfig',
  'ledType': 'GRB',
  'px': 30,
  'brightness': 100,
  'driftIntervalMs': 120000,
  'driftFadePct': 50,
  'shuffleSeed': 0,
  'opSeq': 0,
  'ip': ?ip,
});

Future<ProviderContainer> _container(
  InMemoryBleClient ble, {
  required String status,
  required String config,
}) async {
  SharedPreferences.setMockInitialValues({});
  await seedControlBle(ble, deviceId: _devId);
  await ble.connect(_devId);
  ble.seedSection(_devId, 'wispstatus', _b(status));
  ble.seedSection(_devId, 'wispconfig', _b(config));
  final c = ProviderContainer(
    overrides: [
      bleClientProvider.overrideWithValue(ble),
      lampNearbyPeersNotifierProvider(_devId).overrideWith(() => _FakePeers()),
    ],
  );
  await c.read(inventoryNotifierProvider.future);
  await c
      .read(inventoryNotifierProvider.notifier)
      .add(const InventoryLamp(id: _devId, name: 'testlamp'));
  return c;
}

Future<void> _pumpScreen(WidgetTester tester, ProviderContainer c) async {
  // Tall enough that the OSC panel's last section builds without scrolling.
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
    if (find.text('Palette').evaluate().isNotEmpty) break;
  }
  await tester.pump();
}

int _pollConfigs(InMemoryBleClient ble) => ble
    .writesTo(_devId, BleUuids.wispOp)
    .where((w) => utf8.decode(w, allowMalformed: true).contains('pollConfig'))
    .length;

void main() {
  group('OSC send-to address', () {
    testWidgets('shows the address and port from the wisp settings', (
      tester,
    ) async {
      final ble = InMemoryBleClient();
      final c = await _container(
        ble,
        status: _status(wifi: true),
        config: _config(ip: '192.168.1.42'),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      expect(find.text('OSC'), findsOneWidget);
      expect(find.text('192.168.1.42'), findsOneWidget);
      expect(find.textContaining('9000'), findsOneWidget);
      expect(find.text(_noAddress), findsNothing);
    });

    testWidgets('an empty address shows the no-address line', (tester) async {
      final ble = InMemoryBleClient();
      final c = await _container(
        ble,
        status: _status(),
        config: _config(ip: ''),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      expect(find.text(_noAddress), findsOneWidget);
      expect(find.byKey(const Key('wisp-osc-send-to')), findsNothing);
    });

    testWidgets('a wisp without ip falls back to its Ethernet address', (
      tester,
    ) async {
      final ble = InMemoryBleClient();
      final c = await _container(
        ble,
        status: _status(ethIp: '10.0.0.5'),
        config: _config(),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      expect(find.text('10.0.0.5'), findsOneWidget);
      expect(find.text(_noAddress), findsNothing);
    });

    testWidgets('a wisp without ip or Ethernet shows no address section', (
      tester,
    ) async {
      final ble = InMemoryBleClient();
      final c = await _container(
        ble,
        status: _status(wifi: true),
        config: _config(),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      expect(find.text('LISTENING ON'), findsNothing);
      expect(find.text(_noAddress), findsNothing);
    });

    testWidgets('tapping the address copies it', (tester) async {
      String? copied;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            copied = (call.arguments as Map)['text'] as String?;
          }
          return null;
        },
      );
      addTearDown(
        () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
          SystemChannels.platform,
          null,
        ),
      );
      final ble = InMemoryBleClient();
      final c = await _container(
        ble,
        status: _status(wifi: true),
        config: _config(ip: '192.168.1.42'),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);

      await tester.tap(find.byKey(const Key('wisp-osc-send-to')));
      await tester.pump();
      expect(copied, '192.168.1.42');
    });

    // The config frame after a Wi-Fi join was lost: the status says linked,
    // the held config still has no address.
    testWidgets('a status that contradicts the held address re-polls once', (
      tester,
    ) async {
      final ble = _HoldBle()..holdPollStatus = Completer<void>();
      final c = await _container(
        ble,
        status: _status(),
        config: _config(ip: ''),
      );
      addTearDown(c.dispose);
      await _pumpScreen(tester, c);
      expect(c.read(wispNotifierProvider(_devId)).value!.ip, '');
      final before = _pollConfigs(ble);

      ble.simulateNotify(
        _devId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(_status(wifi: false, lastSeenMs: 2000)),
      );
      await tester.pump();
      await tester.runAsync(() => Future<void>.delayed(Duration.zero));
      await tester.pump();
      expect(c.read(wispNotifierProvider(_devId)).value!.lastSeenMs, 2000);
      expect(_pollConfigs(ble), before, reason: 'agreeing status');

      ble.simulateNotify(
        _devId,
        BleUuids.controlService,
        BleUuids.wispStatus,
        _b(_status(seen: 100, wifi: true)),
      );
      await tester.pump();
      await tester.runAsync(() => Future<void>.delayed(Duration.zero));
      await tester.pump();
      expect(_pollConfigs(ble), before + 1);

      ble.holdPollStatus!.complete();
      await tester.runAsync(() => Future<void>.delayed(Duration.zero));
      await tester.pump();
      expect(
        _pollConfigs(ble),
        before + 1,
        reason: 'the open-time poll is inside the rate limit',
      );
    });
  });

  group('send-to address in the model', () {
    test('wispConfig ip parses, and is null when absent', () {
      expect(WispConfig.fromBytes(_b(_config(ip: '10.1.2.3')))!.ip, '10.1.2.3');
      expect(WispConfig.fromBytes(_b(_config(ip: '')))!.ip, '');
      expect(WispConfig.fromBytes(_b(_config()))!.ip, isNull);
    });

    test('a status merges the config ip and compares on it', () {
      final config = WispConfig.fromBytes(_b(_config(ip: '10.1.2.3')))!;
      const base = WispStatus(wispMac: _mac, configSplit: true);
      final merged = base.withConfig(config);
      expect(merged.ip, '10.1.2.3');
      expect(merged.oscSendTo, '10.1.2.3');
      final other = base.withConfig(
        WispConfig.fromBytes(_b(_config(ip: '10.1.2.4')))!,
      );
      expect(merged == other, isFalse);
      expect(
        merged ==
            base.withConfig(WispConfig.fromBytes(_b(_config(ip: '10.1.2.3')))!),
        isTrue,
      );
    });

    test('on Ethernet the send-to address is the Ethernet one', () {
      const s = WispStatus(
        wispMac: _mac,
        configSplit: true,
        configKnown: true,
        ethIp: '10.0.0.5',
        ip: '192.168.1.42',
      );
      expect(s.oscSendTo, '10.0.0.5');
    });

    // Stamps are the lamp's millis when it cached each report.
    test('the fresher of status and config picks the address', () {
      WispStatus s({
        required int? status,
        required int? config,
        String? ip = '192.168.1.42',
      }) => WispStatus(
        wispMac: _mac,
        configSplit: true,
        configKnown: true,
        wifiConnected: true,
        ethIp: '10.0.0.5',
        ip: ip,
        statusLastSeenMs: status,
        heldConfigSeenMs: config,
      );
      // Ethernet dropped: the edge's status was lost, its config arrived.
      final configNewer = s(status: 100, config: 200);
      expect(configNewer.oscSendTo, '192.168.1.42');
      expect(configNewer.ipDisagreesWithLink, isFalse);
      // Ethernet came up: the config was lost, the status arrived.
      final statusNewer = s(status: 300, config: 200);
      expect(statusNewer.oscSendTo, '10.0.0.5');
      expect(statusNewer.ipDisagreesWithLink, isTrue);
      // Across the lamp's millis wrap.
      final wrapped = s(status: 5, config: 0xFFFFFFF0);
      expect(wrapped.oscSendTo, '10.0.0.5');
      // A merged config read with no stamp is not taken as the fresher.
      expect(s(status: 100, config: null).oscSendTo, '10.0.0.5');
      expect(s(status: 100, config: null).ipDisagreesWithLink, isTrue);
      // A wisp that predates ip shows its Ethernet address whatever the age.
      expect(s(status: 100, config: 200, ip: null).oscSendTo, '10.0.0.5');
    });

    test('an optimistic copy keeps the held ip', () {
      const s = WispStatus(wispMac: _mac, configKnown: true, ip: '10.1.2.3');
      expect(s.copyWith(oscRole: OscRole.fill).ip, '10.1.2.3');
      expect(s.copyWith(brightness: 5).ip, '10.1.2.3');
    });

    test('a config without ip clears one merged before', () {
      const base = WispStatus(wispMac: _mac, configSplit: true);
      final withIp = base.withConfig(
        WispConfig.fromBytes(_b(_config(ip: '10.1.2.3')))!,
      );
      expect(
        withIp.withConfig(WispConfig.fromBytes(_b(_config()))!).ip,
        isNull,
      );
    });

    test('link and held address disagree when the status contradicts ip', () {
      WispStatus s({required String? ip, bool wifi = false, String eth = ''}) =>
          WispStatus(
            wispMac: _mac,
            configSplit: true,
            configKnown: true,
            wifiConnected: wifi,
            ethIp: eth,
            ip: ip,
          );
      expect(s(ip: '', wifi: true).ipDisagreesWithLink, isTrue);
      expect(s(ip: '', eth: '10.0.0.5').ipDisagreesWithLink, isTrue);
      expect(s(ip: '10.0.0.9').ipDisagreesWithLink, isTrue);
      expect(s(ip: '').ipDisagreesWithLink, isFalse);
      expect(s(ip: '10.0.0.9', wifi: true).ipDisagreesWithLink, isFalse);
      // A swap to Ethernet whose config frame was lost.
      expect(s(ip: '192.168.1.42', wifi: true, eth: '10.0.0.5')
          .ipDisagreesWithLink, isTrue);
      expect(s(ip: '10.0.0.5', eth: '10.0.0.5').ipDisagreesWithLink, isFalse);
      expect(
        s(ip: null, wifi: true).ipDisagreesWithLink,
        isFalse,
        reason: 'a wisp that predates ip',
      );
    });
  });

  test('setBrightness keeps the held config ip', () async {
    SharedPreferences.setMockInitialValues({});
    final ble = InMemoryBleClient();
    await ble.connect(_devId);
    ble.seedSection(_devId, 'wispstatus', _b(_status(wifi: true)));
    ble.seedSection(_devId, 'wispconfig', _b(_config(ip: '192.168.1.42')));
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.listen(wispNotifierProvider(_devId), (_, _) {});
    await c.read(wispNotifierProvider(_devId).future);
    await pumpEventQueue();
    final n = c.read(wispNotifierProvider(_devId).notifier);

    n.setBrightness(30);
    ble.simulateNotify(
      _devId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      _b(_status(wifi: true)),
    );
    await pumpEventQueue();

    final shown = c.read(wispNotifierProvider(_devId)).value!;
    expect(shown.brightness, 30);
    expect(shown.ip, '192.168.1.42');
  });

  // The lamp stamps each report as it caches it; a config follows its status
  // by at least 250 ms, and the app reads it one section read later.
  group('send-to address across link edges', () {
    Future<(_ConfigHoldBle, ProviderContainer)> open() async {
      SharedPreferences.setMockInitialValues({});
      final ble = _ConfigHoldBle();
      await ble.connect(_devId);
      ble.seedSection(
        _devId,
        'wispstatus',
        _b(_stamped(wifi: true, status: 100, config: 100)),
      );
      ble.seedSection(_devId, 'wispconfig', _b(_config(ip: '192.168.1.42')));
      final c = ProviderContainer(
        overrides: [bleClientProvider.overrideWithValue(ble)],
      );
      addTearDown(c.dispose);
      c.listen(wispNotifierProvider(_devId), (_, _) {});
      await c.read(wispNotifierProvider(_devId).future);
      await pumpEventQueue();
      return (ble, c);
    }

    String shown(ProviderContainer c) =>
        c.read(wispNotifierProvider(_devId)).value!.oscSendTo;

    test('Ethernet up: the address holds while the new config is read',
        () async {
      final (ble, c) = await open();
      expect(shown(c), '192.168.1.42');

      _notifyStatus(ble, _stamped(ethIp: '10.0.0.5', status: 200, config: 100));
      await pumpEventQueue();
      expect(shown(c), '10.0.0.5');

      ble.holdConfig = Completer<void>();
      ble.seedSection(_devId, 'wispconfig', _b(_config(ip: '10.0.0.5')));
      _notifyStatus(ble, _stamped(ethIp: '10.0.0.5', status: 200, config: 450));
      await pumpEventQueue();
      expect(shown(c), '10.0.0.5', reason: 'config landed, not read yet');

      ble.holdConfig!.complete();
      await pumpEventQueue();
      expect(shown(c), '10.0.0.5');
    });

    test('Ethernet down with its status lost: the new config shows', () async {
      final (ble, c) = await open();
      _notifyStatus(ble, _stamped(ethIp: '10.0.0.5', status: 200, config: 100));
      await pumpEventQueue();
      expect(shown(c), '10.0.0.5');

      // The lamp still holds the Ethernet status; only the config arrives.
      ble.seedSection(_devId, 'wispconfig', _b(_config(ip: '192.168.1.42')));
      _notifyStatus(ble, _stamped(ethIp: '10.0.0.5', status: 200, config: 450));
      await pumpEventQueue();
      expect(shown(c), '192.168.1.42');
    });
  });
}

/// Holds `wispconfig` reads while [holdConfig] is set.
class _ConfigHoldBle extends InMemoryBleClient {
  Completer<void>? holdConfig;

  @override
  Future<Uint8List> readSection(String deviceId, String name) async {
    if (name == 'wispconfig') await holdConfig?.future;
    return super.readSection(deviceId, name);
  }
}

/// A wisp status as the lamp merges it, with its cache stamps.
String _stamped({
  bool wifi = false,
  String ethIp = '',
  required int status,
  required int config,
}) =>
    jsonEncode({
      'wispMac': _mac,
      'configSplit': true,
      'source': 'osc',
      'wifiConnected': wifi,
      if (ethIp.isNotEmpty) 'ethIp': ethIp,
      'statusLastSeenMs': status,
      'configLastSeenMs': config,
    });

void _notifyStatus(InMemoryBleClient ble, String json) => ble.simulateNotify(
      _devId,
      BleUuids.controlService,
      BleUuids.wispStatus,
      _b(json),
    );
