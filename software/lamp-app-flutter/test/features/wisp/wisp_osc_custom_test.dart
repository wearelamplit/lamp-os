import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart' show Override;
import 'package:shared_preferences/shared_preferences.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/application/osc_address_presets.dart';
import 'package:lamp_app/features/wisp/application/wisp_notifier.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
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

// A phone whose preset store refuses writes while [refuse] is set.
class _RefusingPresets extends OscAddressPresets {
  bool refuse = true;

  @override
  Future<bool> store(List<String> next) async =>
      refuse ? false : super.store(next);
}

const _devId = 'lamp-osc-custom';
const _mac = 'AA:BB:CC:DD:EE:FF';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

String _status({
  int seen = 100,
  bool live = false,
  String mac = _mac,
  String source = 'osc',
}) => jsonEncode({
  'wispMac': mac,
  'configSplit': true,
  'source': source,
  'sourceLive': live,
  'manualId': '',
  'configLastSeenMs': seen,
});

/// A `wispConfig` with [mode] and [addr] as given; a null [addr] leaves the
/// key out, as a wisp that predates Custom sends.
String _config({String mode = 'straylight', String? addr = ''}) => jsonEncode({
  'char': 'wispConfig',
  'ledType': 'GRB',
  'px': 30,
  'brightness': 100,
  'driftIntervalMs': 120000,
  'driftFadePct': 50,
  'shuffleSeed': 0,
  'opSeq': 0,
  'ip': '192.168.1.42',
  'swatches': false,
  'oscMode': mode,
  'oscSlot': 2,
  'oscAddr': ?addr,
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

const _wispSrc = '../wisp';

// The wisp's own address cases, read from its test so the two cannot drift.
// Each C string literal is unescaped to its bytes, then read as the UTF-8 a
// phone would send for it.
List<(String, bool)> _wispAddressCases() {
  final src = File(
    '$_wispSrc/test/test_osc_address/osc_address_cases.hpp',
  ).readAsStringSync();
  final body = src.substring(src.indexOf('kOscAddressCases[]'));
  final cases = [
    for (final m in RegExp(
      r'\{"((?:[^"\\]|\\.)*)",\s*(true|false)\}',
    ).allMatches(body))
      (_unescapeC(m[1]!), m[2] == 'true'),
  ];
  final caseLines = RegExp(r'^\s*\{"', multiLine: true).allMatches(body);
  expect(cases, hasLength(caseLines.length), reason: 'every case parsed');
  return cases;
}

String _unescapeC(String lit) {
  final bytes = <int>[];
  for (var i = 0; i < lit.length; i++) {
    if (lit[i] != r'\') {
      bytes.addAll(utf8.encode(lit[i]));
      continue;
    }
    final e = lit[++i];
    switch (e) {
      case 'x':
        var j = i + 1;
        while (j < lit.length && RegExp('[0-9a-fA-F]').hasMatch(lit[j])) {
          j++;
        }
        final byte = int.parse(lit.substring(i + 1, j), radix: 16);
        expect(byte, lessThan(0x100));
        bytes.add(byte);
        i = j - 1;
      case 't':
        bytes.add(0x09);
      case 'n':
        bytes.add(0x0A);
      case '"' || r'\' || "'":
        bytes.add(e.codeUnitAt(0));
      default:
        fail('escape \\$e is not handled here');
    }
  }
  return utf8.decode(bytes);
}

void main() {
  group('Custom address rule', () {
    test("matches every one of the wisp's cases", () {
      final cases = _wispAddressCases();
      expect(cases.where((c) => c.$2), isNotEmpty);
      expect(cases.where((c) => !c.$2), isNotEmpty);
      for (final (address, valid) in cases) {
        expect(
          oscCustomAddressValid(address),
          valid,
          reason: jsonEncode(address),
        );
      }
    });

    test("caps at the wisp's kOscCustomAddressMaxLen", () {
      final hpp = File('$_wispSrc/src/osc/osc_target.hpp').readAsStringSync();
      final cap = RegExp(r'kOscCustomAddressMaxLen = (\d+);').firstMatch(hpp);
      expect(int.parse(cap!.group(1)!), kOscCustomAddressMaxLen);
    });

    test('names what is wrong', () {
      expect(oscCustomAddressProblem(''), OscAddressProblem.empty);
      expect(oscCustomAddressProblem('a/b'), OscAddressProblem.noLeadingSlash);
      expect(
        oscCustomAddressProblem('/${'a' * 15}'),
        OscAddressProblem.tooLong,
      );
      expect(oscCustomAddressProblem('/a b'), OscAddressProblem.badCharacter);
      expect(oscCustomAddressProblem('/lamp/palette'), isNull);
    });
  });

  group('Custom on the wire', () {
    Future<(InMemoryBleClient, WispRepository)> repo() async {
      final ble = InMemoryBleClient();
      await ble.connect(_devId);
      return (ble, WispRepository(ble, _devId));
    }

    test('sends mode and address in one op', () async {
      final (ble, r) = await repo();
      await r.setOsc(OscMode.custom, address: '/lamp/palette');
      expect(_oscOps(ble), [
        {
          'char': 'wispOp',
          'op': 'setOsc',
          'mode': 'custom',
          'address': '/lamp/palette',
        },
      ]);
    });

    test('never sends Custom without an address the wisp takes', () async {
      final (ble, r) = await repo();
      await expectLater(r.setOsc(OscMode.custom), throwsArgumentError);
      await expectLater(
        r.setOsc(OscMode.custom, address: 'lamp'),
        throwsArgumentError,
      );
      await expectLater(
        r.setOsc(OscMode.maestro, address: '/a b'),
        throwsArgumentError,
      );
      expect(ble.writesTo(_devId, BleUuids.wispOp), isEmpty);
    });
  });

  group('Custom in the notifier', () {
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
      ble.seedSection(_devId, 'wispconfig', _b(config ?? _config()));
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

    test('confirms only on Custom with the address sent', () async {
      final (ble, c, n) = await open();

      var confirmed = false;
      final done = n
          .setCustomOscAddress('/lamp/palette')
          .then((_) => confirmed = true);
      await pumpEventQueue();
      expect(
        (shown(c).oscMode, shown(c).oscAddr),
        (OscMode.custom, '/lamp/palette'),
      );
      expect(_oscOps(ble).single, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'custom',
        'address': '/lamp/palette',
      });

      // Custom on another address, then the address stored under another
      // sender: neither is this pick landing.
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'custom', addr: '/other')),
      );
      _notify(ble, _status(seen: 200));
      await Future<void>.delayed(const Duration(milliseconds: 60));
      ble.seedSection(_devId, 'wispconfig', _b(_config(addr: '/lamp/palette')));
      _notify(ble, _status(seen: 300));
      await Future<void>.delayed(const Duration(milliseconds: 60));
      expect(confirmed, isFalse);
      expect(shown(c).oscMode, OscMode.custom, reason: 'the pick still holds');

      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'custom', addr: '/lamp/palette')),
      );
      _notify(ble, _status(seen: 400));
      await done;
      expect(confirmed, isTrue);
      expect(shown(c).oscAddr, '/lamp/palette');
    });

    test('goes back and throws when the wisp never takes it', () async {
      final (ble, c, n) = await open(config: _config(addr: '/old'));

      Object? error;
      final done = n
          .setCustomOscAddress('/lamp/palette')
          .catchError((Object e) => error = e);
      await pumpEventQueue();
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'custom', addr: '/old')),
      );
      _notify(ble, _status(seen: 200));
      await done;

      expect(error, isA<Exception>());
      expect(shown(c).oscAddr, '/old');
      expect(shown(c).oscMode, OscMode.custom, reason: "the wisp's own state");
      expect(
        _oscOps(ble).every((op) => op['address'] == '/lamp/palette'),
        isTrue,
      );
    });

    test('rolls back to the old sender when nothing lands', () async {
      final (ble, c, n) = await open();

      Object? error;
      await n
          .setCustomOscAddress('/lamp/palette')
          .catchError((Object e) => error = e);

      expect(error, isNotNull);
      expect((shown(c).oscMode, shown(c).oscAddr), (OscMode.straylight, ''));
      expect(_oscOps(ble).length, greaterThan(1), reason: 'resent on a miss');
    });

    test('a slot still confirming rides along with a Custom pick', () async {
      final (ble, _, n) = await open(config: _config(mode: 'maestro'));

      final slot = n.setMaestroSlot(5).catchError((Object _) {});
      await pumpEventQueue();
      final custom = n.setCustomOscAddress('/x').catchError((Object _) {});
      await pumpEventQueue();

      expect(_oscOps(ble).last, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'custom',
        'slot': 5,
        'address': '/x',
      });
      await Future.wait([slot, custom]);
    });

    test(
      'an address the wisp refuses sends nothing and shows nothing',
      () async {
        final (ble, c, n) = await open();
        final before = shown(c);

        await expectLater(n.setCustomOscAddress('/a b'), throwsArgumentError);
        await expectLater(n.setCustomOscAddress(''), throwsArgumentError);

        expect(ble.writesTo(_devId, BleUuids.wispOp), isEmpty);
        expect(shown(c), before);
      },
    );

    test('Custom picked as a sender sends the stored address', () async {
      final (ble, _, n) = await open(config: _config(addr: '/kept'));

      final done = n.setOscMode(OscMode.custom).catchError((Object _) {});
      await pumpEventQueue();
      expect(_oscOps(ble).first, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'custom',
        'address': '/kept',
      });
      await done;
    });

    test('Custom picked with no stored address sends nothing', () async {
      final (ble, c, n) = await open();

      await expectLater(n.setOscMode(OscMode.custom), throwsArgumentError);
      expect(ble.writesTo(_devId, BleUuids.wispOp), isEmpty);
      expect(shown(c).oscMode, OscMode.straylight);
    });

    test('Maestro still sends no address and confirms without one', () async {
      final (ble, c, n) = await open(config: _config(addr: '/kept'));

      final done = n.setOscMode(OscMode.maestro);
      await pumpEventQueue();
      expect(_oscOps(ble).single, {
        'char': 'wispOp',
        'op': 'setOsc',
        'mode': 'maestro',
      });
      ble.seedSection(
        _devId,
        'wispconfig',
        _b(_config(mode: 'maestro', addr: '/kept')),
      );
      _notify(ble, _status(seen: 200));
      await done;
      expect(shown(c).oscMode, OscMode.maestro);
      expect(shown(c).oscAddr, '/kept');
    });
  });

  group('Custom in the model', () {
    test('oscAddr parses; absent is null', () {
      final m = WispConfig.fromBytes(_b(_config(mode: 'custom', addr: '/x')))!;
      expect((m.oscMode, m.oscAddr), (OscMode.custom, '/x'));
      expect(WispConfig.fromBytes(_b(_config()))!.oscAddr, '');
      expect(WispConfig.fromBytes(_b(_config(addr: null)))!.oscAddr, isNull);
    });

    test('a status merges it, compares on it, and copies keep it', () {
      const base = WispStatus(wispMac: _mac, configSplit: true);
      WispStatus merged(String addr) => base.withConfig(
        WispConfig.fromBytes(_b(_config(mode: 'custom', addr: addr)))!,
      );
      expect(merged('/x').oscAddr, '/x');
      expect(merged('/x') == merged('/y'), isFalse);
      expect(merged('/x') == merged('/x'), isTrue);
      expect(merged('/x').copyWith(brightness: 10).oscAddr, '/x');
      final config = WispConfig.fromBytes(_b(_config(addr: '/x')))!;
      expect(config.copyWith(brightness: 10).oscAddr, '/x');
    });
  });

  group('presets', () {
    test('kept on the phone across a restart, valid ones only', () async {
      SharedPreferences.setMockInitialValues({});
      final c1 = ProviderContainer();
      addTearDown(c1.dispose);
      final p1 = c1.read(oscAddressPresetsProvider.notifier);
      await p1.add('/a');
      await p1.add('/b');
      await p1.add('/a');
      await p1.add('bad');
      await p1.remove('/b');
      await p1.add('/c');

      final c2 = ProviderContainer();
      addTearDown(c2.dispose);
      expect(await c2.read(oscAddressPresetsProvider.future), ['/a', '/c']);
    });

    test('a stored address the rule refuses is not offered', () async {
      SharedPreferences.setMockInitialValues({
        'flutter.wisp_osc_presets.v1': ['/ok', 'bad', '/a b'],
      });
      final c = ProviderContainer();
      addTearDown(c.dispose);
      expect(await c.read(oscAddressPresetsProvider.future), ['/ok']);
    });

    test(
      'a change the phone fails to store throws and changes nothing',
      () async {
        SharedPreferences.setMockInitialValues({
          'flutter.wisp_osc_presets.v1': ['/a'],
        });
        final refusing = _RefusingPresets();
        final c = ProviderContainer(
          overrides: [oscAddressPresetsProvider.overrideWith(() => refusing)],
        );
        addTearDown(c.dispose);
        final p = c.read(oscAddressPresetsProvider.notifier);
        await c.read(oscAddressPresetsProvider.future);

        await expectLater(p.add('/b'), throwsStateError);
        await expectLater(p.remove('/a'), throwsStateError);
        expect(c.read(oscAddressPresetsProvider).value, ['/a']);

        refusing.refuse = false;
        await p.add('/b');
        expect(c.read(oscAddressPresetsProvider).value, ['/a', '/b']);
      },
    );

    test('changes made together all land', () async {
      SharedPreferences.setMockInitialValues({
        'flutter.wisp_osc_presets.v1': ['/a', '/b'],
      });
      final c1 = ProviderContainer();
      addTearDown(c1.dispose);
      final p = c1.read(oscAddressPresetsProvider.notifier);
      await c1.read(oscAddressPresetsProvider.future);
      await Future.wait([p.remove('/a'), p.add('/c'), p.remove('/b')]);

      final c2 = ProviderContainer();
      addTearDown(c2.dispose);
      expect(await c2.read(oscAddressPresetsProvider.future), ['/c']);
    });
  });

  group('Custom on screen', () {
    Future<(InMemoryBleClient, ProviderContainer)> container({
      required String config,
      String? status,
      List<String> presets = const [],
      List<Override> overrides = const [],
    }) async {
      SharedPreferences.setMockInitialValues({
        'flutter.wisp_osc_presets.v1': presets,
      });
      final ble = InMemoryBleClient();
      await seedControlBle(ble, deviceId: _devId);
      await ble.connect(_devId);
      ble.seedSection(_devId, 'wispstatus', _b(status ?? _status()));
      ble.seedSection(_devId, 'wispconfig', _b(config));
      final c = ProviderContainer(
        overrides: [
          bleClientProvider.overrideWithValue(ble),
          lampNearbyPeersNotifierProvider(
            _devId,
          ).overrideWith(() => _FakePeers()),
          ...overrides,
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
        await c.read(oscAddressPresetsProvider.future);
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

    // Run out the unconfirmed resends.
    Future<void> drain(WidgetTester tester) async {
      await tester.pump(const Duration(seconds: 45));
      await tester.runAsync(() => pumpEventQueue());
    }

    final modeSwitch = find.byKey(const Key('wisp-osc-mode'));
    final field = find.byKey(const Key('wisp-osc-custom-address'));
    final sendButton = find.byKey(const Key('wisp-osc-custom-send'));
    final sendTo = find.byKey(const Key('wisp-osc-send-to'));

    Set<OscMode> selected(WidgetTester tester) =>
        tester.widget<SegmentedButton<OscMode>>(modeSwitch).selected;
    bool sendEnabled(WidgetTester tester) =>
        tester.widget<IconButton>(sendButton).onPressed != null;

    testWidgets('a wisp without Custom: disabled, asks for a wisp update', (
      tester,
    ) async {
      final (ble, c) = await container(config: _config(addr: null));
      addTearDown(c.dispose);
      await pump(tester, c);

      final custom = tester
          .widget<SegmentedButton<OscMode>>(modeSwitch)
          .segments
          .singleWhere((s) => s.value == OscMode.custom);
      expect(custom.enabled, isFalse);
      expect(find.text('Update this wisp to use Custom.'), findsOneWidget);
      expect(find.byKey(const Key('wisp-osc-role-picker')), findsOneWidget);

      await tester.tap(find.text('Custom'));
      await tester.pump();
      expect(selected(tester), {OscMode.straylight});
      expect(field, findsNothing);
      expect(_oscOps(ble), isEmpty);
    });

    testWidgets('a wisp with Custom offers it with no update line', (
      tester,
    ) async {
      final (_, c) = await container(config: _config());
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(find.text('Straylight'), findsOneWidget);
      expect(find.text('Maestro'), findsOneWidget);
      expect(find.text('Custom'), findsOneWidget);
      expect(find.byKey(const Key('wisp-osc-update-custom')), findsNothing);
    });

    testWidgets('Custom with no address: explains, sends nothing until one', (
      tester,
    ) async {
      final (ble, c) = await container(config: _config());
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.text('Custom'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(selected(tester), {OscMode.custom});
      expect(field, findsOneWidget);
      expect(
        find.text('Custom needs an address before the wisp can switch.'),
        findsOneWidget,
      );
      expect(sendEnabled(tester), isFalse);
      expect(find.textContaining('Waiting for'), findsNothing);
      expect(sendTo, findsOneWidget);
      expect(_oscOps(ble), isEmpty);

      await tester.enterText(field, 'lamp');
      await tester.pump();
      expect(find.text('Start with /.'), findsOneWidget);
      expect(sendEnabled(tester), isFalse);
      await tester.testTextInput.receiveAction(TextInputAction.send);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());
      expect(_oscOps(ble), isEmpty);

      await tester.enterText(field, '/lamp/palette');
      await tester.pump();
      expect(sendEnabled(tester), isTrue);
      await tester.tap(sendButton);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());
      expect(_oscOps(ble), [
        {
          'char': 'wispOp',
          'op': 'setOsc',
          'mode': 'custom',
          'address': '/lamp/palette',
        },
      ]);
      expect(selected(tester), {OscMode.custom});
      await drain(tester);
    });

    testWidgets('Custom with a stored address sends it at once', (
      tester,
    ) async {
      final (ble, c) = await container(config: _config(addr: '/kept'));
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.text('Custom'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(_oscOps(ble), [
        {
          'char': 'wispOp',
          'op': 'setOsc',
          'mode': 'custom',
          'address': '/kept',
        },
      ]);
      expect(tester.widget<TextField>(field).controller!.text, '/kept');
      await drain(tester);
    });

    testWidgets('Custom: the address, its wait, its help and the send-to row', (
      tester,
    ) async {
      final (_, c) = await container(
        config: _config(mode: 'custom', addr: '/lamp/palette'),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      expect(selected(tester), {OscMode.custom});
      expect(tester.widget<TextField>(field).controller!.text, '/lamp/palette');
      expect(sendEnabled(tester), isFalse, reason: 'already the address');
      expect(
        find.textContaining('Waiting for OSC on /lamp/palette.'),
        findsOneWidget,
      );
      expect(
        find.textContaining(
          'Send palettes on /lamp/palette to 192.168.1.42, port 9000.',
        ),
        findsOneWidget,
      );
      expect(sendTo, findsOneWidget);
      expect(find.byKey(const Key('wisp-osc-role-picker')), findsNothing);
    });

    testWidgets('a preset sends; save and delete keep them on the phone', (
      tester,
    ) async {
      final (ble, c) = await container(
        config: _config(mode: 'custom', addr: '/live'),
        presets: ['/saved'],
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.byKey(const Key('wisp-osc-save-preset')));
      await tester.runAsync(() => pumpEventQueue());
      await tester.pump();
      expect(c.read(oscAddressPresetsProvider).value, ['/saved', '/live']);
      expect(find.byKey(const Key('wisp-osc-save-preset')), findsNothing);

      await tester.tap(find.text('/saved'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());
      expect(_oscOps(ble), [
        {
          'char': 'wispOp',
          'op': 'setOsc',
          'mode': 'custom',
          'address': '/saved',
        },
      ]);

      await tester.tap(
        find.descendant(
          of: find.byKey(const Key('wisp-osc-preset-/live')),
          matching: find.byTooltip('Delete preset'),
        ),
      );
      await tester.runAsync(() => pumpEventQueue());
      await tester.pump();
      expect(c.read(oscAddressPresetsProvider).value, ['/saved']);
      final prefs = await SharedPreferences.getInstance();
      expect(prefs.getStringList('wisp_osc_presets.v1'), ['/saved']);
      await drain(tester);
    });

    testWidgets('a preset the phone fails to store says so', (tester) async {
      final (_, c) = await container(
        config: _config(mode: 'custom', addr: '/live'),
        overrides: [
          oscAddressPresetsProvider.overrideWith(_RefusingPresets.new),
        ],
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.byKey(const Key('wisp-osc-save-preset')));
      await tester.runAsync(() => pumpEventQueue());
      await tester.pump();

      expect(find.text("Couldn't save presets. Try again."), findsOneWidget);
      expect(find.byKey(const Key('wisp-osc-preset-/live')), findsNothing);
      expect(find.byKey(const Key('wisp-osc-save-preset')), findsOneWidget);
    });

    testWidgets('a pick the wisp never confirms says so', (tester) async {
      final (ble, c) = await container(config: _config());
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.text('Custom'));
      await tester.pump();
      await tester.enterText(field, '/lamp/palette');
      await tester.testTextInput.receiveAction(TextInputAction.send);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());
      expect(_oscOps(ble), hasLength(1));

      await drain(tester);
      await tester.pump();
      expect(find.text("Couldn't reach the wisp. Try again."), findsOneWidget);
      expect(selected(tester), {OscMode.straylight});
    });

    String fieldText(WidgetTester tester) =>
        tester.widget<TextField>(field).controller!.text;

    Future<void> deliver(
      WidgetTester tester,
      InMemoryBleClient ble, {
      String? config,
      required String status,
    }) async {
      await tester.runAsync(() async {
        if (config != null) ble.seedSection(_devId, 'wispconfig', _b(config));
        _notify(ble, status);
        await Future<void>.delayed(const Duration(milliseconds: 300));
      });
      for (var i = 0; i < 10; i++) {
        await tester.pump(const Duration(milliseconds: 16));
      }
    }

    testWidgets('an address pick the wisp never takes goes back in the field', (
      tester,
    ) async {
      final (ble, c) = await container(
        config: _config(mode: 'custom', addr: '/a'),
        presets: ['/b'],
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.text('/b'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());
      expect(fieldText(tester), '/b');

      await drain(tester);
      await tester.pump();
      expect(find.text("Couldn't reach the wisp. Try again."), findsOneWidget);
      expect(fieldText(tester), '/a');
      expect(sendEnabled(tester), isFalse);
    });

    testWidgets('an address arriving while typing leaves the typing alone', (
      tester,
    ) async {
      final (ble, c) = await container(
        config: _config(mode: 'custom', addr: '/a'),
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.enterText(field, '/typing');
      await deliver(
        tester,
        ble,
        config: _config(mode: 'custom', addr: '/c'),
        status: _status(seen: 200),
      );

      expect(
        find.textContaining('Send palettes on /c'),
        findsOneWidget,
        reason: 'the new address was read',
      );
      expect(fieldText(tester), '/typing');
    });

    Future<(InMemoryBleClient, ProviderContainer)> draftUp(
      WidgetTester tester,
    ) async {
      final (ble, c) = await container(config: _config());
      addTearDown(c.dispose);
      await pump(tester, c);
      await tester.tap(find.text('Custom'));
      await tester.pump();
      expect(selected(tester), {OscMode.custom});
      return (ble, c);
    }

    testWidgets('Custom with no address clears when the source changes', (
      tester,
    ) async {
      final (ble, _) = await draftUp(tester);

      await deliver(tester, ble, status: _status(source: 'manual'));
      expect(modeSwitch, findsNothing);
      await deliver(tester, ble, status: _status());

      expect(selected(tester), {OscMode.straylight});
      expect(field, findsNothing);
    });

    testWidgets('Custom with no address clears when another wisp shows', (
      tester,
    ) async {
      final (ble, _) = await draftUp(tester);

      await deliver(
        tester,
        ble,
        config: _config(),
        status: _status(seen: 200, mac: '11:22:33:44:55:66'),
      );
      await deliver(
        tester,
        ble,
        status: _status(seen: 300, mac: '11:22:33:44:55:66'),
      );

      expect(selected(tester), {OscMode.straylight});
      expect(field, findsNothing);
    });

    testWidgets('Custom with no address gives way to the wisp going Custom', (
      tester,
    ) async {
      final (ble, _) = await draftUp(tester);

      await deliver(
        tester,
        ble,
        config: _config(mode: 'custom', addr: '/x'),
        status: _status(seen: 200),
      );

      expect(fieldText(tester), '/x');
      expect(find.textContaining('Waiting for OSC on /x.'), findsOneWidget);
      expect(_oscOps(ble), isEmpty);
    });

    testWidgets('leaving Custom with no address for the wisp sender sends '
        'nothing', (tester) async {
      final (ble, _) = await draftUp(tester);

      await tester.tap(find.text('Straylight'));
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(selected(tester), {OscMode.straylight});
      expect(_oscOps(ble), isEmpty);
    });

    testWidgets('the address already in use is not sent again', (tester) async {
      final (ble, c) = await container(
        config: _config(mode: 'custom', addr: '/a'),
        presets: ['/a'],
      );
      addTearDown(c.dispose);
      await pump(tester, c);

      await tester.tap(find.byKey(const Key('wisp-osc-preset-/a')));
      await tester.pump();
      await tester.showKeyboard(field);
      await tester.testTextInput.receiveAction(TextInputAction.send);
      await tester.pump();
      await tester.runAsync(() => pumpEventQueue());

      expect(_oscOps(ble), isEmpty);
    });
  });
}
