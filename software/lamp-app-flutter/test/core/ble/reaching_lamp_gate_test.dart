import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart' show Override;
import 'package:lamp_app/core/ble/reaching_lamp_gate.dart';
import 'package:lamp_app/core/routing/router.dart';
import 'package:lamp_app/core/routing/routes.dart';
import 'package:lamp_app/features/control/application/control_notifier.dart';
import 'package:lamp_app/features/control/application/control_state.dart';
import 'package:lamp_app/features/control/domain/sections.dart';
import 'package:lamp_app/features/social/domain/social_mode.dart';

const _lampId = 'lamp-x';
const _lampName = 'gramp';

ControlState _fakeState({required bool connected}) => ControlState(
      lamp: const LampSection(
        name: _lampName,
        brightness: 100,
        advancedEnabled: false,
        webappEnabled: true,
        socialMode: SocialMode.ambivert,
      ),
      base: const BaseSection(
          px: 35, bpp: 4, byteOrder: 'GRBW', colors: [], knockout: {}),
      shade: const ShadeSection(px: 38, bpp: 4, byteOrder: 'GRBW', colors: []),
      home: const HomeSection(
        ssid: '',
        brightness: 60,
        enabled: false,
        networkBound: false,
        socialDisabled: true,
        disabledExpressionTypes: [],
      ),
      expressions: const ExpressionsSection(expressions: []),
      connected: connected,
    );

class _FakeControl extends ControlNotifier {
  _FakeControl({required this.connected});
  final bool connected;
  @override
  Future<ControlState> build(String deviceId) async =>
      _fakeState(connected: connected);
}

/// Never resolves, so the provider stays AsyncLoading for the test's
/// duration.
class _PendingControl extends ControlNotifier {
  @override
  Future<ControlState> build(String deviceId) => Completer<ControlState>().future;
}

class _ErrorControl extends ControlNotifier {
  @override
  Future<ControlState> build(String deviceId) async =>
      throw Exception('lamp not found');
}

GoRouter _testRouter(String initialLocation) => GoRouter(
      initialLocation: initialLocation,
      routes: [
        GoRoute(
          path: AppRoutes.myLamps,
          builder: (_, _) => const Scaffold(body: Text('My Lamps')),
        ),
        GoRoute(
          path: '/lamp/:id/control',
          builder: (_, _) => const Scaffold(body: Text('lamp screen')),
        ),
      ],
    );

Future<void> _pumpGate(
  WidgetTester tester, {
  required GoRouter router,
  List<Override> overrides = const [],
}) {
  return tester.pumpWidget(
    ProviderScope(
      overrides: [appRouterProvider.overrideWithValue(router), ...overrides],
      child: MaterialApp.router(
        routerConfig: router,
        builder: (context, child) =>
            ReachingLampGate(child: child ?? const SizedBox.shrink()),
      ),
    ),
  );
}

void main() {
  group('activeLampId', () {
    test('matches /lamp/:id/control', () {
      expect(activeLampId('/lamp/abc/control'), 'abc');
    });
    test('matches /lamp/:id/expressions', () {
      expect(activeLampId('/lamp/abc/expressions'), 'abc');
    });
    test('matches /lamp/:id/expressions/new', () {
      expect(activeLampId('/lamp/abc/expressions/new'), 'abc');
    });
    test('matches /lamp/:id/expressions/:type/:target', () {
      expect(activeLampId('/lamp/abc/expressions/glitchy/2'), 'abc');
    });
    test('matches /lamp/:id/setup and its sub-routes', () {
      expect(activeLampId('/lamp/abc/setup'), 'abc');
      expect(activeLampId('/lamp/abc/setup/home-mode'), 'abc');
      expect(activeLampId('/lamp/abc/setup/wifi'), 'abc');
      expect(activeLampId('/lamp/abc/setup/advanced-leds'), 'abc');
      expect(activeLampId('/lamp/abc/setup/knockout'), 'abc');
    });
    test('matches /lamp/:id/wisp', () {
      expect(activeLampId('/lamp/abc/wisp'), 'abc');
    });
    test('rejects a non-lamp route', () {
      expect(activeLampId('/lamps'), isNull);
      expect(activeLampId('/onboarding'), isNull);
    });
    test('rejects a route that merely starts with control/expressions', () {
      expect(activeLampId('/lamp/abc/controlpanel'), isNull);
    });
  });

  testWidgets('overlay shown while the control provider is loading',
      (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId).overrideWith(() => _PendingControl()),
      ],
    );
    await tester.pump();
    expect(find.text('← pick another lamp'), findsOneWidget);
    // Name isn't known yet during the initial load; the gate falls back
    // to a generic line rather than showing a stale/incorrect name.
    expect(find.textContaining('your lamp'), findsOneWidget);
  });

  testWidgets('overlay shown when the lamp is not connected', (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: false)),
      ],
    );
    await tester.pump();
    await tester.pump();
    expect(find.text('← pick another lamp'), findsOneWidget);
    expect(find.textContaining(_lampName), findsOneWidget);
  });

  testWidgets('overlay absent once the lamp is connected', (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: true)),
      ],
    );
    await tester.pump();
    await tester.pump();
    expect(find.text('← pick another lamp'), findsNothing);
    expect(find.text('lamp screen'), findsOneWidget);
  });

  testWidgets('overlay hidden when the control provider errors', (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId).overrideWith(() => _ErrorControl()),
      ],
    );
    await tester.pump();
    await tester.pump();
    expect(find.text('← pick another lamp'), findsNothing);
    expect(find.text('lamp screen'), findsOneWidget);
  });

  testWidgets('rotates to a different line after 4 seconds', (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: false)),
      ],
    );
    await tester.pump();
    await tester.pump();

    Text lineText() => tester.widget<Text>(find.byWidgetPredicate(
        (w) => w is Text && w.data != null && w.data!.contains(_lampName)));
    final firstLine = lineText().data;

    await tester.pump(const Duration(seconds: 4));
    await tester.pump(const Duration(milliseconds: 450));

    expect(lineText().data, isNot(firstLine));
  });

  testWidgets(
      'pointer passes through to the child when the overlay is hidden',
      (tester) async {
    var tapped = false;
    final router = GoRouter(
      initialLocation: '/lamp/$_lampId/control',
      routes: [
        GoRoute(
          path: '/lamp/:id/control',
          builder: (_, _) => Scaffold(
            body: ElevatedButton(
              onPressed: () => tapped = true,
              child: const Text('lamp screen'),
            ),
          ),
        ),
      ],
    );
    await _pumpGate(
      tester,
      router: router,
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: true)),
      ],
    );
    await tester.pump();
    await tester.pump();

    await tester.tap(find.text('lamp screen'));
    await tester.pump();

    expect(tapped, isTrue);
  });

  testWidgets('overlay absent on a route that is not a lamp connection screen',
      (tester) async {
    await _pumpGate(tester, router: _testRouter(AppRoutes.myLamps));
    await tester.pump();
    expect(find.text('← pick another lamp'), findsNothing);
    expect(find.text('My Lamps'), findsOneWidget);
  });

  testWidgets('blocks back-navigation while the overlay is shown',
      (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: false)),
      ],
    );
    await tester.pump();
    await tester.pump();

    final popScope = tester.widget<PopScope>(
        find.ancestor(of: find.byType(Stack), matching: find.byType(PopScope))
            .first);
    expect(popScope.canPop, isFalse);
  });

  testWidgets('allows back-navigation once the overlay is hidden',
      (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: true)),
      ],
    );
    await tester.pump();
    await tester.pump();

    final popScope = tester.widget<PopScope>(
        find.ancestor(of: find.byType(Stack), matching: find.byType(PopScope))
            .first);
    expect(popScope.canPop, isTrue);
  });

  testWidgets('escape button navigates to My Lamps', (tester) async {
    await _pumpGate(
      tester,
      router: _testRouter('/lamp/$_lampId/control'),
      overrides: [
        controlNotifierProvider(_lampId)
            .overrideWith(() => _FakeControl(connected: false)),
      ],
    );
    await tester.pump();
    await tester.pump();

    await tester.tap(find.text('← pick another lamp'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));

    expect(find.text('My Lamps'), findsOneWidget);
  });
}
