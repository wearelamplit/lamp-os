import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_client.dart';
import 'package:lamp_app/core/ble/ble_timing.dart';
import 'package:lamp_app/core/ble/connect_with_retry.dart';
import 'package:lamp_app/core/ble/discovery_cache.dart';
import 'package:lamp_app/core/ble/fbp_ble_client.dart';
import 'package:lamp_app/core/ble/network_dev_ble_client.dart';
import 'package:lamp_app/core/ble/uuids.dart';

import '../../_support/fake_bridge_transport.dart';
import '../../_support/in_memory_ble_client.dart';

class _Clock {
  Duration t = Duration.zero;
  void advance(int ms) => t += Duration(milliseconds: ms);
}

/// A client whose connects take scripted fake time.
class _TimedClient extends InMemoryBleClient {
  _TimedClient(this.clock, this.connectCosts);
  final _Clock clock;
  final List<({int ms, bool fails})> connectCosts;
  void Function()? whileConnecting;
  Object failure = StateError('connect');

  @override
  Future<void> connect(String deviceId) async {
    final c = connectCosts.removeAt(0);
    clock.advance(c.ms);
    whileConnecting?.call();
    if (c.fails) throw failure;
    await super.connect(deviceId);
  }
}

void main() {
  late _Clock clock;
  late List<BleTimingEvent> events;
  late void Function(BleTimingEvent) savedSink;
  late Duration Function() savedNow;

  setUp(() {
    clock = _Clock();
    events = [];
    savedSink = bleTimings.sink;
    savedNow = bleTimings.now;
    bleTimings
      ..sink = events.add
      ..now = (() => clock.t);
  });
  tearDown(() {
    bleTimings
      ..sink = savedSink
      ..now = savedNow;
    for (final id in ['x', 'y', 'dev']) {
      bleTimings.linkDown(id);
    }
  });

  Iterable<BleTimingEvent> firstReads() =>
      events.where((e) => e.phase == BleTimingPhase.firstRead);

  Future<void> seedEmptyPages(BleClient ble, String id) =>
      ble.write(id, BleUuids.controlService, BleUuids.pageData, Uint8List(0));

  group('connect', () {
    test(
      'records link and total time, then the first read since link up',
      () async {
        final ble = _TimedClient(clock, [(ms: 300, fails: false)]);
        await connectWithRetry(ble, 'x', owner: Object());
        await seedEmptyPages(ble, 'x');
        clock.advance(450);
        await readSectionVia(ble, 'x', 'lamp');
        expect(events.map((e) => e.phase), [
          BleTimingPhase.connect,
          BleTimingPhase.firstRead,
        ]);
        expect(events[0].ms, 300);
        expect(events[0].attempt, 1);
        expect(events[1].section, 'lamp');
        expect(
          events[1].sinceLinkMs,
          450,
          reason: 'fake time stands still during the read itself',
        );
        expect(events[1].ms, 0);
      },
    );

    test(
      'a failed attempt then a success records the attempt number',
      () async {
        final ble = _TimedClient(clock, [
          (ms: 8000, fails: true),
          (ms: 300, fails: false),
        ]);
        await connectWithRetry(
          ble,
          'x',
          owner: Object(),
          backoff: Duration.zero,
        );
        expect(events.length, 2);
        expect(events[0].ok, isFalse);
        expect(events[0].attempt, 1);
        expect(events[0].ms, 8000);
        expect(events[1].ok, isTrue);
        expect(events[1].attempt, 2);
        expect(events[1].attempts, 4);
        expect(events[1].ms, 300);
        expect(events[1].totalMs, 8300);
      },
    );

    test(
      'the last failed attempt is recorded and the error still thrown',
      () async {
        final ble = _TimedClient(clock, [(ms: 700, fails: true)]);
        await expectLater(
          connectWithRetry(ble, 'x', owner: Object(), attempts: 1),
          throwsStateError,
        );
        expect(events.single.ok, isFalse);
        expect(events.single.ms, 700);
      },
    );

    test('an attempt that began on an already-up link is flagged', () async {
      final ble = _TimedClient(clock, [
        (ms: 100, fails: false),
        (ms: 5, fails: false),
      ]);
      await connectWithRetry(ble, 'x', owner: Object());
      await connectWithRetry(ble, 'x', owner: Object());
      expect(events[0].note, isNull);
      expect(events[1].note, 'already_up');
    });

    test(
      'an abort after the link comes up records an aborted attempt',
      () async {
        final ble = _TimedClient(clock, [(ms: 7000, fails: false)]);
        var abort = false;
        ble.whileConnecting = () => abort = true;
        expect(
          await connectWithRetry(
            ble,
            'x',
            owner: Object(),
            shouldAbort: () => abort,
          ),
          isFalse,
        );
        expect(events.single.note, 'aborted');
        expect(events.single.ms, 7000);
        expect(events.single.totalMs, isNull);
        bleTimings.sectionRead('x', 'lamp', Duration.zero);
        expect(
          firstReads(),
          isEmpty,
          reason: 'an aborted attempt arms nothing',
        );
      },
    );
  });

  group('first read', () {
    test(
      'is recorded once per link and re-armed by the next connect',
      () async {
        final ble = _TimedClient(clock, [
          (ms: 100, fails: false),
          (ms: 100, fails: false),
        ]);
        await connectWithRetry(ble, 'x', owner: Object());
        await seedEmptyPages(ble, 'x');
        await readSectionVia(ble, 'x', 'lamp');
        await readSectionVia(ble, 'x', 'base');
        expect(firstReads().length, 1);

        await connectWithRetry(ble, 'x', owner: Object());
        await readSectionVia(ble, 'x', 'shade');
        expect(firstReads().map((e) => e.section), ['lamp', 'shade']);
      },
    );

    test(
      'a link that went down leaves no arm for a later plain connect',
      () async {
        final ble = _TimedClient(clock, [
          (ms: 100, fails: false),
          (ms: 100, fails: false),
        ]);
        await connectWithRetry(ble, 'x', owner: Object());
        await seedEmptyPages(ble, 'x');
        bleTimings.linkDown('x');
        clock.advance(30000);
        await ble.connect('x');
        await readSectionVia(ble, 'x', 'lamp');
        expect(firstReads(), isEmpty);
      },
    );

    test('a read begun before a newer link came up leaves that link armed', () {
      final oldReadStart = clock.t;
      clock.advance(500);
      bleTimings.connectSucceeded('y', Duration.zero, Duration.zero, 1, 4);
      bleTimings.sectionRead('y', 'lamp', oldReadStart);
      expect(firstReads(), isEmpty);
      clock.advance(10);
      bleTimings.sectionRead('y', 'lamp', clock.t);
      expect(firstReads().length, 1);
    });

    test(
      'a first read that throws is recorded as a failure and rethrown',
      () async {
        final ble = _TimedClient(clock, [(ms: 100, fails: false)]);
        await connectWithRetry(ble, 'x', owner: Object());
        await ble.disconnect('x');
        bleTimings.connectSucceeded('x', Duration.zero, Duration.zero, 1, 4);
        await expectLater(
          readSectionVia(ble, 'x', 'lamp'),
          throwsA(isA<BleNotConnected>()),
        );
        expect(firstReads().single.ok, isFalse);
      },
    );

    test('the network-dev client disarms when its link drops', () async {
      final c = NetworkDevBleClient(FakeBridgeTransport());
      await c.connect('dev');
      bleTimings.connectSucceeded('dev', Duration.zero, Duration.zero, 1, 4);
      await c.disconnect('dev');
      clock.advance(10);
      bleTimings.sectionRead('dev', 'lamp', clock.t);
      expect(firstReads(), isEmpty);
    });
  });

  group('discover', () {
    test('times a cache miss and not a hit', () async {
      final cache = DiscoveryCache<int>();
      Future<int> slow() async {
        clock.advance(230);
        return 7;
      }

      expect(await cache.resolve('x', slow), 7);
      expect(await cache.resolve('x', slow), 7);
      expect(events.single.phase, BleTimingPhase.discover);
      expect(events.single.ms, 230);

      cache.remove('x');
      await cache.resolve('x', slow);
      expect(events.length, 2);
    });
  });

  test(
    'a discover that throws is recorded as a failure and rethrown',
    () async {
      final cache = DiscoveryCache<int>();
      final err = StateError('discover');
      await expectLater(
        cache.resolve('x', () async {
          clock.advance(15000);
          throw err;
        }),
        throwsA(same(err)),
      );
      expect(events.single.ok, isFalse);
      expect(events.single.ms, 15000);
      expect(
        await cache.resolve('x', () async => 1),
        1,
        reason: 'a failure is not cached',
      );
    },
  );

  group('fbp client link drops', () {
    test('disconnect disarms the first read', () async {
      bleTimings.connectSucceeded('x', Duration.zero, Duration.zero, 1, 4);
      try {
        await FbpBleClient()
            .disconnect('x')
            .timeout(const Duration(seconds: 2));
      } catch (_) {}
      clock.advance(10);
      bleTimings.sectionRead('x', 'lamp', clock.t);
      expect(firstReads(), isEmpty);
    });

    test('cycleAdapter disarms the first read', () async {
      bleTimings.connectSucceeded('x', Duration.zero, Duration.zero, 1, 4);
      try {
        await FbpBleClient()
            .cycleAdapter('x')
            .timeout(const Duration(seconds: 2));
      } catch (_) {}
      clock.advance(10);
      bleTimings.sectionRead('x', 'lamp', clock.t);
      expect(firstReads(), isEmpty);
    });
  });

  test(
    'a section read nobody awaits does not raise an uncaught error',
    () async {
      final ble = InMemoryBleClient();
      final uncaught = <Object>[];
      await runZonedGuarded(() async {
        unawaited(readSectionVia(ble, 'x', 'lamp'));
        await Future<void>.delayed(const Duration(milliseconds: 20));
      }, (e, _) => uncaught.add(e));
      expect(uncaught, isEmpty);
    },
  );

  test('a sighting before any scan start records nothing', () {
    (BleTimings()..sink = events.add).sighting('x');
    expect(events, isEmpty);
  });

  group('scan seen', () {
    test('first sighting per lamp per scan session, from scan start', () {
      bleTimings.scanStarted();
      clock.advance(900);
      bleTimings.sighting('x');
      clock.advance(600);
      bleTimings.sighting('x');
      bleTimings.sighting('y');
      expect(events.map((e) => (e.deviceId, e.ms, e.scanSession)), [
        ('x', 900, events[0].scanSession),
        ('y', 1500, events[0].scanSession),
      ]);
    });

    test('a scan restart opens a new session timed from its own start', () {
      bleTimings.scanStarted();
      clock.advance(400);
      bleTimings.sighting('x');
      clock.advance(10000);
      bleTimings.scanStarted();
      clock.advance(250);
      bleTimings.sighting('x');
      expect(events.map((e) => (e.ms, e.scanSession)), [
        (400, events[0].scanSession),
        (250, events[0].scanSession! + 1),
      ]);
    });
  });

  test('a throwing sink does not fail a connect', () async {
    bleTimings.sink = (_) => throw StateError('sink');
    final ble = _TimedClient(clock, [(ms: 10, fails: false)]);
    expect(await connectWithRetry(ble, 'x', owner: Object()), isTrue);
  });

  group('failure reason', () {
    test(
      'a failed connect attempt carries the fbp code and description',
      () async {
        final ble =
            _TimedClient(clock, [
                (ms: 300, fails: true),
                (ms: 50, fails: false),
              ])
              ..failure = fbp.FlutterBluePlusException(
                fbp.ErrorPlatform.android,
                'connect',
                133,
                'ANDROID_SPECIFIC_ERROR gatt error',
              );
        await connectWithRetry(
          ble,
          'x',
          owner: Object(),
          backoff: Duration.zero,
        );
        expect(
          events[0].toString(),
          '[ble_timing] connect id=x fail attempt=1/4 ms=300 '
          'reason=FlutterBluePlusException:133:ANDROID_SPECIFIC_ERROR_gatt_error',
        );
        expect(events[1].reason, isNull);
        expect(events[1].toString(), isNot(contains('reason=')));
      },
    );

    test('a generic failure carries its type and no spaces', () async {
      final ble = _TimedClient(clock, [(ms: 700, fails: true)]);
      await expectLater(
        connectWithRetry(ble, 'x', owner: Object(), attempts: 1),
        throwsStateError,
      );
      expect(events.single.reason, 'StateError');
      expect(events.single.toString(), endsWith(' reason=StateError'));
    });

    test('a missing code or description renders as a dash', () {
      expect(
        bleFailureReason(
          fbp.FlutterBluePlusException(
            fbp.ErrorPlatform.android,
            'connect',
            null,
            null,
          ),
        ),
        'FlutterBluePlusException:-:-',
      );
    });

    test('a failed discover carries the reason', () async {
      await expectLater(
        DiscoveryCache<int>().resolve(
          'x',
          () async => throw TimeoutException('t'),
        ),
        throwsA(isA<TimeoutException>()),
      );
      expect(events.single.reason, 'TimeoutException');
    });

    test('a failed first read carries the reason', () {
      bleTimings.connectSucceeded('x', clock.t, clock.t, 1, 4);
      events.clear();
      bleTimings.sectionRead('x', 'lamp', clock.t, error: StateError('r'));
      expect(events.single.ok, isFalse);
      expect(events.single.reason, 'StateError');
    });
  });

  test('an event renders as one greppable line', () {
    const e = BleTimingEvent(
      BleTimingPhase.connect,
      'AA',
      300,
      attempt: 2,
      attempts: 4,
      totalMs: 9500,
    );
    expect(
      e.toString(),
      '[ble_timing] connect id=AA ok attempt=2/4 ms=300 total_ms=9500',
    );
  });
}
