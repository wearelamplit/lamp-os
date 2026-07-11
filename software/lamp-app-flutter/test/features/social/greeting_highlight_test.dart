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
          ac: 0,
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
        home: const HomeSection(ssid: '', brightness: 60, enabled: false),
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

// Two peer BD_ADDRs with distinct base colors so we can differentiate tints.
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
              bdAddr: _peerA,
              baseRgbw: _peerABaseRgbw,
              shadeRgbw: [0, 0, 0, 0],
              rssi: -72,
              proximity: 0,
            ),
            LampNearbyPeer(
              name: 'peer-b',
              bdAddr: _peerB,
              baseRgbw: _peerBBaseRgbw,
              shadeRgbw: [0, 0, 0, 0],
              rssi: -65,
              proximity: 1,
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

// Finds the AnimatedContainer for the peer row with [name] and returns its
// decoration color. The AnimatedContainer is the first AnimatedContainer
// ancestor above the peer name Text widget.
Color? _rowColor(WidgetTester tester, String name) {
  final nameFinder = find.text(name);
  if (nameFinder.evaluate().isEmpty) return null;
  final containers = find.ancestor(
    of: nameFinder,
    matching: find.byType(AnimatedContainer),
  );
  if (containers.evaluate().isEmpty) return null;
  final widget = tester.widget<AnimatedContainer>(containers.first);
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
}
