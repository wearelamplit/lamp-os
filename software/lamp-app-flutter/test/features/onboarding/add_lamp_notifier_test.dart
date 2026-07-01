import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import '../../_support/in_memory_ble_client.dart';
import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
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

  test('leaving mid-claim aborts before the claim write — no blank blob',
      () async {
    // The user taps Close during the ~1-3s claim window. reset() (via the
    // shell's dispose) fires while submit() awaits the step-0 connect; submit
    // must bail before building the claim blob from the now-empty state, which
    // would otherwise write {password:'', name:'', setup:true} to the lamp.
    late AddLampNotifier n;
    final ble = _ResetDuringClaimBleClient(() => n.reset());
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword('secret');
    await n.submit();

    expect(ble.writes, isNot(contains(BleUuids.settingsBlob)),
        reason: 'must not write the claim blob after the user left');
    final s = c.read(addLampNotifierProvider);
    expect(s.step, AddLampStep.scan,
        reason: 'reset returned the wizard to scan; submit must not clobber it');
    expect(s.deviceId, isEmpty);
  });

  test('reset() cancels the in-flight verify — no state write after leaving',
      () async {
    // Regression for the orphaned-verify bug: the provider is keepAlive, so
    // `ref.mounted` never trips in production. Leaving the wizard mid-reconnect
    // fires reset() (via the shell's dispose); the fire-and-forget verify must
    // observe that and bail, not write ready/error onto — or keep hammering
    // BLE for — a wizard the user already left.
    AddLampNotifier.verifyConnectTimeout = const Duration(milliseconds: 20);
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
    n.setPassword(''); // skip path: the reconnect is the whole verify
    await n.submit();
    final inFlight = n.verifyDone; // capture before reset() nulls it

    // User backs out mid-reconnect → shell dispose → reset().
    n.reset();
    await inFlight; // orphan must bail cleanly, not resurrect state

    final s = c.read(addLampNotifierProvider);
    expect(s.status, AddLampStatus.idle,
        reason: 'cancelled verify must not flip status to ready/error');
    expect(s.step, AddLampStep.scan);
    expect(s.deviceId, isEmpty,
        reason: 'reset cleared the wizard; the orphan must not un-reset it');
  });

  test('retryVerify recovers to ready once the lamp comes back', () async {
    // The QA-flagged recoverable path: first verify exhausts to connectFailed,
    // then the lamp answers and the user taps Retry → status reaches ready.
    AddLampNotifier.verifyConnectTimeout = const Duration(milliseconds: 20);
    AddLampNotifier.reconnectAttempts = 3;
    addTearDown(() => AddLampNotifier.reconnectAttempts = 12);
    final ble = _RecoveringBleClient();
    final c = ProviderContainer(
      overrides: [bleClientProvider.overrideWithValue(ble)],
    );
    addTearDown(c.dispose);
    await c.read(inventoryNotifierProvider.future);
    await c.read(activeLampNotifierProvider.future);

    final n = c.read(addLampNotifierProvider.notifier);
    n.select('dev1');
    n.setName('jacko');
    n.setPassword(''); // skip path
    await n.submit();
    await n.verifyDone!.timeout(const Duration(seconds: 5));

    var s = c.read(addLampNotifierProvider);
    expect(s.status, AddLampStatus.error);
    expect(s.error, AddLampError.connectFailed);

    // Lamp is back; Retry must reach ready.
    ble.recovered = true;
    n.retryVerify();
    await n.verifyDone!.timeout(const Duration(seconds: 5));

    s = c.read(addLampNotifierProvider);
    expect(s.status, AddLampStatus.ready,
        reason: 'Retry after the lamp recovers must reach ready');
    expect(s.error, AddLampError.none);
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

/// Fires [onFirstConnect] during the step-0 claim connect, so a test can
/// simulate the user leaving the wizard (reset()) mid-claim. Records the
/// characteristic UUIDs written so the test can assert the claim blob was
/// never sent.
class _ResetDuringClaimBleClient implements BleClient {
  _ResetDuringClaimBleClient(this.onFirstConnect);

  final void Function() onFirstConnect;
  int _connectCount = 0;
  final List<String> writes = [];
  final Set<String> _connected = {};

  @override
  Future<void> prewarm(String deviceId) async {}

  @override
  Future<void> connect(String deviceId) async {
    _connectCount++;
    if (_connectCount == 1) onFirstConnect();
    _connected.add(deviceId);
  }

  @override
  Future<void> disconnect(String deviceId) async => _connected.remove(deviceId);

  @override
  bool isConnected(String deviceId) => _connected.contains(deviceId);

  @override
  Future<Uint8List> read(String d, String s, String c) async => Uint8List(0);

  @override
  Future<Uint8List> readSection(String deviceId, String name) async =>
      Uint8List(0);

  @override
  Future<void> write(
    String d,
    String s,
    String c,
    Uint8List v, {
    bool withoutResponse = false,
    bool allowLongWrite = false,
  }) async {
    writes.add(c);
  }

  @override
  Stream<Uint8List> subscribe(String d, String s, String c) =>
      const Stream.empty();

  @override
  Stream<bool> watchConnected(String deviceId) =>
      Stream.value(_connected.contains(deviceId));

  @override
  Future<void> cycleAdapter(String deviceId) async => _connected.remove(deviceId);
}

/// Fails every verify-phase reconnect until [recovered] is set, then connects
/// cleanly — models the lamp finally coming back after the user taps Retry.
/// The step-0 claim connect (first call) always succeeds.
class _RecoveringBleClient implements BleClient {
  bool recovered = false;
  int _connectCount = 0;
  final Set<String> _connected = {};

  @override
  Future<void> prewarm(String deviceId) async {}

  @override
  Future<void> connect(String deviceId) async {
    _connectCount++;
    if (_connectCount > 1 && !recovered) {
      throw const BleDisconnectedException('dev1');
    }
    _connected.add(deviceId);
  }

  @override
  Future<void> disconnect(String deviceId) async {
    _connected.remove(deviceId);
  }

  @override
  bool isConnected(String deviceId) => _connected.contains(deviceId);

  @override
  Future<Uint8List> read(String d, String s, String c) async => Uint8List(0);

  @override
  Future<Uint8List> readSection(String deviceId, String name) async =>
      Uint8List(0);

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
