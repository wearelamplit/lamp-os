import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/features/inventory/application/active_lamp_notifier.dart';
import 'package:lamp_app/features/inventory/application/inventory_notifier.dart';
import 'package:lamp_app/features/onboarding/application/add_lamp_notifier.dart';
import 'package:lamp_app/features/onboarding/domain/add_lamp_state.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  setUp(() {
    SharedPreferences.setMockInitialValues({});
    AddLampNotifier.verifyDelay = Duration.zero;
    AddLampNotifier.verifySkipDelay = Duration.zero;
    AddLampNotifier.reconnectSettleDelay = Duration.zero;
    AddLampNotifier.reconnectBackoff = Duration.zero;
  });
  tearDown(() {
    AddLampNotifier.verifyDelay = const Duration(seconds: 5);
    AddLampNotifier.verifySkipDelay = const Duration(seconds: 2);
    AddLampNotifier.verifyConnectTimeout = const Duration(seconds: 15);
    AddLampNotifier.verifyOpTimeout = const Duration(seconds: 10);
    AddLampNotifier.reconnectSettleDelay = const Duration(milliseconds: 500);
    AddLampNotifier.reconnectBackoff = const Duration(milliseconds: 1500);
  });

  test('select(deviceId) sets the id and advances to adoptConfirm step', () async {
    final ble = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    c.read(addLampNotifierProvider.notifier).select('dev1');
    final s = c.read(addLampNotifierProvider);
    expect(s.deviceId, 'dev1');
    expect(s.step, AddLampStep.adoptConfirm);
  });

  test('next() from adoptConfirm goes to name', () {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(InMemoryBleClient())],
    );
    addTearDown(c.dispose);
    final n = c.read(addLampNotifierProvider.notifier);
    n.select('lamp-x');
    n.next();
    expect(c.read(addLampNotifierProvider).step, AddLampStep.name);
  });

  test('previous() from adoptConfirm goes to scan', () {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(InMemoryBleClient())],
    );
    addTearDown(c.dispose);
    final n = c.read(addLampNotifierProvider.notifier);
    n.select('lamp-x');
    n.previous();
    expect(c.read(addLampNotifierProvider).step, AddLampStep.scan);
  });

  test('previous() from name goes to scan', () {
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(InMemoryBleClient())],
    );
    addTearDown(c.dispose);
    final n = c.read(addLampNotifierProvider.notifier);
    n.select('lamp-x');
    n.next(); // adoptConfirm → name
    n.previous();
    expect(c.read(addLampNotifierProvider).step, AddLampStep.scan);
  });

  test('meet is gone and adoptConfirm is present in AddLampStep', () {
    final names = AddLampStep.values.map((e) => e.name).toList();
    expect(names, contains('adoptConfirm'));
    expect(names, isNot(contains('meet')));
    expect(names, isNot(contains('connecting')));
  });

  test('setName and setPassword update fields', () {
    final ble = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    final n = c.read(addLampNotifierProvider.notifier);
    n.setName('jacko');
    n.setPassword('secret');
    final s = c.read(addLampNotifierProvider);
    expect(s.name, 'jacko');
    expect(s.password, 'secret');
  });

  test('submit() with valid post-claim lampSection persists + sets active',
      () async {
    final ble = InMemoryBleClient();
    // Seed the lamp section so the post-claim probe finds a valid JSON.
    ble.seedSection(
      'dev1',
      'lamp',
      Uint8List.fromList(utf8.encode('{"name":"jacko","brightness":50}')),
    );

    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('secret');
    await n.submit();
    await n.verifyDone;

    // Claim done + verified: the Meet pane is up with Continue enabled.
    var s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.verifying);
    expect(s.status, AddLampStatus.ready);
    expect(s.error, AddLampError.none);

    // Continue persists + advances.
    await n.finishAdoption();
    s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.done);
    expect(s.status, AddLampStatus.idle);

    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv.map((l) => l.id).toList(), ['dev1']);
    expect(inv.first.controlPassword, 'secret');

    final active = await c.read(activeLampNotifierProvider.future);
    expect(active, 'dev1');
  });

  test('submit() bounces back to password step on wrong password', () async {
    // Seed lampSection with empty bytes so the post-claim read finds the
    // characteristic but returns no data — the auth-gate behavior. The
    // notifier throws FormatException('auth-rejected'), which the typed
    // catch maps to AddLampError.wrongPassword.
    final ble = InMemoryBleClient();
    // Empty bytes simulate the firmware's auth-gated empty response.
    ble.seedSection('dev1', 'lamp', Uint8List(0));

    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('wrong');
    await n.submit();
    await n.verifyDone;

    final s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.password);
    expect(s.status, AddLampStatus.error);
    expect(s.error, AddLampError.wrongPassword);

    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv, isEmpty,
        reason: 'wrong password must not persist the lamp');
  });

  test('submit() surfaces wrongPassword when post-claim read returns empty',
      () async {
    // Don't seed the lamp section — readSection returns Uint8List(0)
    // which jsonDecode rejects with FormatException, which the
    // notifier maps to wrongPassword. Under the page protocol an
    // unseeded section is indistinguishable from an auth-gated empty
    // response on the firmware side; both surface as wrongPassword.
    final ble = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('secret');
    await n.submit();
    await n.verifyDone;

    final s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.password);
    expect(s.status, AddLampStatus.error);
    expect(s.error, AddLampError.wrongPassword);
    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv, isEmpty);
  });

  test('submit() with empty password (Skip) succeeds without probing auth',
      () async {
    // Skip path: password is empty. The lamp's isAuthed() early-returns
    // true for empty passwords, so the auth+read probe is short-circuited.
    // We just need the post-reboot reconnect to land.
    final ble = InMemoryBleClient();
    // Deliberately do NOT seed lampSection — Skip path doesn't read it.
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword(''); // explicit Skip
    await n.submit();
    await n.verifyDone;

    var s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.verifying,
        reason: 'Skip path must reconnect and enable Continue, not hang');
    expect(s.status, AddLampStatus.ready);
    expect(s.error, AddLampError.none);

    await n.finishAdoption();
    s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.done);
    expect(s.status, AddLampStatus.idle);

    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv.map((l) => l.id).toList(), ['dev1']);
    expect(inv.first.controlPassword, '');
  });

  test('Skip path surfaces recoverable error when the lamp never reconnects',
      () async {
    // Skip path where every post-reboot connect stalls → the retry loop
    // exhausts and the Meet pane shows a recoverable connectFailed with a
    // Retry, rather than trapping the user or dumping them out.
    AddLampNotifier.verifyConnectTimeout = const Duration(milliseconds: 20);
    AddLampNotifier.reconnectAttempts = 3;
    addTearDown(() => AddLampNotifier.reconnectAttempts = 12);
    final ble = _HangingBleClient(hangOn: _HangOp.connect);
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('');
    await n.submit();
    await n.verifyDone!.timeout(const Duration(seconds: 5));

    final s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.verifying,
        reason: 'stays on the Meet pane so the user can Retry');
    expect(s.status, AddLampStatus.error);
    expect(s.error, AddLampError.connectFailed);
    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv, isEmpty);
  });

  test('submit() with password surfaces timeout on stalled read', () async {
    // Non-skip path with a stalled read → recoverable timeout error.
    AddLampNotifier.verifyOpTimeout = const Duration(milliseconds: 50);
    final ble = _HangingBleClient(hangOn: _HangOp.read);
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('secret');
    await n.submit();
    await n.verifyDone!.timeout(const Duration(seconds: 5));

    final s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.verifying,
        reason: 'stays on the Meet pane so the user can Retry');
    expect(s.status, AddLampStatus.error);
    expect(s.error, AddLampError.connectFailed,
        reason: 'timeout must surface as recoverable connectFailed');
  });

  test('add(deviceId, name) skips wizard and adds to inventory', () async {
    final ble = InMemoryBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    await c
        .read(addLampNotifierProvider.notifier)
        .add(deviceId: 'dev2', name: 'melonie');

    final inv = await c.read(inventoryNotifierProvider.future);
    expect(inv.map((l) => l.name).toList(), ['melonie']);

    final active = await c.read(activeLampNotifierProvider.future);
    expect(active, 'dev2');
  });
}

enum _HangOp { connect, read }

/// BleClient that hangs forever on the selected op AFTER the initial setup
/// connect succeeds. submit() calls connect() once for the claim, then
/// connect() again as part of the post-reboot verify probe — we want the
/// second connect to hang so the verify-path timeout fires, not the
/// step-0 connect.
class _HangingBleClient implements BleClient {
  _HangingBleClient({required this.hangOn});

  final _HangOp hangOn;
  int _connectCount = 0;
  final Set<String> _connected = {};

  @override
  Future<void> prewarm(String deviceId) async {}

  @override
  Future<void> connect(String deviceId) {
    _connectCount++;
    // First connect = the step-0 claim connect; let it succeed. Second
    // connect onward = the verify-probe reconnect; hang there.
    if (hangOn == _HangOp.connect && _connectCount > 1) {
      return Completer<void>().future;
    }
    _connected.add(deviceId);
    return Future<void>.value();
  }

  @override
  Future<void> disconnect(String deviceId) async {
    _connected.remove(deviceId);
  }

  @override
  bool isConnected(String deviceId) => _connected.contains(deviceId);

  @override
  Future<Uint8List> read(String d, String s, String c) {
    if (hangOn == _HangOp.read) return Completer<Uint8List>().future;
    return Future.value(Uint8List(0));
  }

  @override
  Future<Uint8List> readSection(String deviceId, String name) {
    // Same hang semantics as raw read — the test exercises the verify
    // probe through the readSection helper now that lampSection moved
    // behind it.
    if (hangOn == _HangOp.read) return Completer<Uint8List>().future;
    return Future.value(Uint8List(0));
  }

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {}

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) =>
      const Stream.empty();

  @override
  Stream<bool> watchConnected(String deviceId) =>
      Stream.value(_connected.contains(deviceId));

  @override
  Future<void> cycleAdapter(String deviceId) async {
    _connected.remove(deviceId);
  }
}
