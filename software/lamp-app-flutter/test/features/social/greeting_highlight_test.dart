// Widget tests for the greeting-state row highlight on the Social tab.
//
// Scenarios:
// 1. Active greeting for peer X: row for X shows a tinted background using
//    X's base color (not transparent); other rows are not tinted.
// 2. No active greeting (greeting == null): no row is tinted.
//
// Test wiring mirrors social_screen_test.dart: fake ControlNotifier returns
// a synthetic ControlState so we can inject arbitrary greeting values;
// fake LampNearbyPeers avoids BLE polling. The widget assertion uses the
// AnimatedContainer's decoration color as the observable signal — we check
// that it is non-transparent for the greeted row and transparent otherwise.

import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/utils/string_case.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/social/application/lamp_nearby_peers_notifier.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/social/domain/social_mode.dart';
import 'package:lamp_app/features/social/presentation/social_screen.dart';
import 'package:shared_preferences/shared_preferences.dart';

// Greeting-state-aware fake control notifier. Returns a synthetic ControlState
// with the provided GreetingState injected so widget tests don't need BLE.
class _GreetingFakeControl extends ControlNotifier {
  _GreetingFakeControl(this._greeting);
  final GreetingState? _greeting;

  @override
  Future<ControlState> build(String deviceId) async => ControlState(
        lamp: LampSection(
          name: 'self',
          brightness: 100,
          advancedEnabled: false,
          webappEnabled: true,
          socialMode: SocialMode.ambivert,
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
        greeting: _greeting,
      );
}

class _FakeLampNearbyPeers extends LampNearbyPeersNotifier {
  _FakeLampNearbyPeers(this._seed);
  final List<LampNearbyPeer> _seed;
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => _seed;
}

class _MutableFakeLampNearbyPeers extends LampNearbyPeersNotifier {
  _MutableFakeLampNearbyPeers(this._seed);
  List<LampNearbyPeer> _seed;
  @override
  Future<List<LampNearbyPeer>> build(String lampId) async => _seed;

  void setPeers(List<LampNearbyPeer> peers) {
    _seed = peers;
    state = AsyncData(peers);
  }
}

// Seeds CHAR_SOCIAL_DISPOSITIONS so Dispositions.build() doesn't hang.
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

// Two peer lampIds with distinct base colors so we can differentiate tints.
// Peer A: red-ish base (0xFF0000 -> Color(0xFFFF0000))
// Peer B: blue-ish base (0x0000FF -> Color(0xFF0000FF))
const _peerA = 'AA:BB:CC:DD:EE:FF';
const _peerABaseRgbw = [0xFF, 0x00, 0x00, 0x00]; // red
const _peerB = '11:22:33:44:55:66';
const _peerBBaseRgbw = [0x00, 0x00, 0xFF, 0x00]; // blue

Widget _buildApp(
  InMemoryBleClient ble, {
  required GreetingState? greeting,
}) =>
    ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('lamp-id')
            .overrideWith(() => _GreetingFakeControl(greeting)),
        lampNearbyPeersNotifierProvider('lamp-id').overrideWith(
          () => _FakeLampNearbyPeers(const [
            LampNearbyPeer(
              name: 'peer-a',
              lampId: _peerA,
              baseRgbw: _peerABaseRgbw,
              shadeRgbw: [0, 0, 0, 0],
              rssi: -72,
            ),
            LampNearbyPeer(
              name: 'peer-b',
              lampId: _peerB,
              baseRgbw: _peerBBaseRgbw,
              shadeRgbw: [0, 0, 0, 0],
              rssi: -65,
            ),
          ]),
        ),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'lamp-id')),
      ),
    );

Future<void> _pump(WidgetTester tester) async {
  await tester.pump();
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 100));
}

// Finds the row's painted decoration color. AnimatedContainer.decoration
// is always the fresh target config, not what's actually on screen mid
// interpolation; the real painted value lives on the inner Container that
// _AnimatedContainerState.build() constructs each tick, so read that one.
Color? _rowColor(WidgetTester tester, String name) {
  final nameFinder = find.text(name.toTitleCase());
  if (nameFinder.evaluate().isEmpty) return null;
  final animContainers = find.ancestor(
    of: nameFinder,
    matching: find.byType(AnimatedContainer),
  );
  if (animContainers.evaluate().isEmpty) return null;
  final innerContainers = find.descendant(
    of: animContainers.first,
    matching: find.byType(Container),
  );
  if (innerContainers.evaluate().isEmpty) return null;
  final widget = tester.widget<Container>(innerContainers.first);
  final decoration = widget.decoration;
  if (decoration is BoxDecoration) return decoration.color;
  return null;
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));

  testWidgets('active greeting tints greeted peer row with its base color',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(ble, 'lamp-id', {_peerA: 3, _peerB: 3});

    await tester.pumpWidget(_buildApp(
      ble,
      greeting: const GreetingState(peer: _peerA, kind: 'glitch'),
    ));
    await _pump(tester);

    // Peer A is greeted: its row color should be non-transparent.
    final colorA = _rowColor(tester, 'peer-a');
    expect(colorA, isNotNull);
    expect(colorA!.a, greaterThan(0.0),
        reason: 'greeted row should have a tinted background');

    // Peer B is NOT greeted: its row stays transparent.
    final colorB = _rowColor(tester, 'peer-b');
    // transparent or null — either is acceptable; the key signal is no tint.
    if (colorB != null) {
      expect(colorB.a, equals(0.0),
          reason: 'non-greeted row must not be tinted');
    }
  });

  testWidgets('no greeting state leaves all rows transparent', (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(ble, 'lamp-id', {_peerA: 3, _peerB: 3});

    await tester.pumpWidget(_buildApp(ble, greeting: null));
    await _pump(tester);

    for (final name in ['peer-a', 'peer-b']) {
      final color = _rowColor(tester, name);
      if (color != null) {
        expect(color.a, equals(0.0),
            reason: 'row $name must not be tinted when no greeting is active');
      }
    }
  });

  testWidgets(
      'reorder does not bleed the greeted row highlight onto the wrong peer',
      (tester) async {
    final ble = InMemoryBleClient();
    await _seedDispositions(ble, 'lamp-id', {_peerA: 3, _peerB: 3});

    final fakePeers = _MutableFakeLampNearbyPeers(const [
      LampNearbyPeer(
        name: 'peer-a',
        lampId: _peerA,
        baseRgbw: _peerABaseRgbw,
        shadeRgbw: [0, 0, 0, 0],
        rssi: -72,
        near: true,
      ),
      LampNearbyPeer(
        name: 'peer-b',
        lampId: _peerB,
        baseRgbw: _peerBBaseRgbw,
        shadeRgbw: [0, 0, 0, 0],
        rssi: -65,
        near: false,
      ),
    ]);

    await tester.pumpWidget(ProviderScope(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        controlNotifierProvider('lamp-id').overrideWith(() =>
            _GreetingFakeControl(
                const GreetingState(peer: _peerB, kind: 'glitch'))),
        lampNearbyPeersNotifierProvider('lamp-id')
            .overrideWith(() => fakePeers),
      ],
      child: const MaterialApp(
        home: Scaffold(body: SocialScreen(lampId: 'lamp-id')),
      ),
    ));
    await _pump(tester);

    // peer-b is greeted but sorts last (far); peer-a sorts first (near).
    expect(_rowColor(tester, 'peer-a')?.a ?? 0.0, equals(0.0));
    expect(_rowColor(tester, 'peer-b')?.a, greaterThan(0.0));

    // Flip near flags so peer-b now sorts first and peer-a sorts second:
    // the two rows swap slots without either peer's greeting state changing.
    fakePeers.setPeers(const [
      LampNearbyPeer(
        name: 'peer-a',
        lampId: _peerA,
        baseRgbw: _peerABaseRgbw,
        shadeRgbw: [0, 0, 0, 0],
        rssi: -72,
        near: false,
      ),
      LampNearbyPeer(
        name: 'peer-b',
        lampId: _peerB,
        baseRgbw: _peerBBaseRgbw,
        shadeRgbw: [0, 0, 0, 0],
        rssi: -65,
        near: true,
      ),
    ]);
    // Zero elapsed time: catches a reused Element's tween starting from
    // the previous occupant's decoration, before it interpolates away.
    await tester.pump(Duration.zero);

    expect(_rowColor(tester, 'peer-b')?.a, greaterThan(0.0),
        reason: 'still-greeted peer-b must stay tinted after reorder');
    expect(_rowColor(tester, 'peer-a')?.a ?? 0.0, equals(0.0),
        reason:
            'peer-a was never greeted; it must not inherit the tint left '
            'behind by whichever row previously occupied its slot');
  });
}
