import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_timing.dart';
import 'package:lamp_app/core/ble/discovery_cache.dart';

/// A discovery whose runs finish only when the test completes them.
class _Discoveries {
  final runs = <Completer<int>>[];

  Future<int> call() {
    final c = Completer<int>();
    runs.add(c);
    return c.future;
  }
}

void main() {
  late List<BleTimingEvent> events;
  late void Function(BleTimingEvent) savedSink;
  late DiscoveryCache<int> cache;
  late _Discoveries discover;

  setUp(() {
    events = [];
    savedSink = bleTimings.sink;
    bleTimings.sink = events.add;
    cache = DiscoveryCache<int>();
    discover = _Discoveries();
  });
  tearDown(() => bleTimings.sink = savedSink);

  test('a timing failure never touches the result', () async {
    final savedNow = bleTimings.now;
    var calls = 0;
    bleTimings.now = () {
      if (++calls == 2) throw StateError('clock');
      return Duration.zero;
    };
    addTearDown(() => bleTimings.now = savedNow);

    final a = cache.resolve('x', discover.call);
    discover.runs.single.complete(1);
    expect(await a, 1);
    expect(await cache.resolve('x', discover.call), 1);
    expect(discover.runs, hasLength(1));
  });

  test('a discover that removes its own device is not cached', () async {
    final a = cache.resolve('x', () {
      cache.remove('x');
      return discover();
    });
    discover.runs.single.complete(1);
    expect(await a, 1);

    final next = cache.resolve('x', discover.call);
    expect(discover.runs, hasLength(2), reason: 'a fresh discovery');
    discover.runs[1].complete(2);
    expect(await next, 2);
  });

  Iterable<BleTimingEvent> discovers() =>
      events.where((e) => e.phase == BleTimingPhase.discover);

  test('concurrent resolves share one discovery', () async {
    final a = cache.resolve('x', discover.call);
    final b = cache.resolve('x', discover.call);
    final other = cache.resolve('y', discover.call);
    expect(discover.runs, hasLength(2), reason: 'one per device');

    discover.runs[0].complete(41);
    discover.runs[1].complete(9);
    expect(await a, 41);
    expect(await b, 41);
    expect(await other, 9);
    expect(await cache.resolve('x', discover.call), 41);
    expect(discover.runs, hasLength(2));
    expect(discovers().where((e) => e.deviceId == 'x'), hasLength(1));
  });

  test('a failed discovery reaches every waiter and is not cached', () async {
    final a = cache.resolve('x', discover.call);
    final b = cache.resolve('x', discover.call);
    final err = StateError('discover');
    discover.runs.single.completeError(err);
    await expectLater(a, throwsA(same(err)));
    await expectLater(b, throwsA(same(err)));

    final retry = cache.resolve('x', discover.call);
    expect(discover.runs, hasLength(2), reason: 'a fresh discovery');
    discover.runs[1].complete(5);
    expect(await retry, 5);
  });

  test(
    'a discovery in flight across a remove never feeds the later link',
    () async {
      final stale = cache.resolve('x', discover.call);
      cache.remove('x');
      final fresh = cache.resolve('x', discover.call);
      expect(discover.runs, hasLength(2), reason: 'a fresh discovery');

      discover.runs[0].complete(1);
      expect(await stale, 1);
      final joined = cache.resolve('x', discover.call);
      expect(discover.runs, hasLength(2), reason: 'joins the fresh one');

      discover.runs[1].complete(2);
      expect(await fresh, 2);
      expect(await joined, 2);
      expect(await cache.resolve('x', discover.call), 2);
    },
  );

  test('a stale discovery failing leaves the fresh one shared', () async {
    final stale = cache.resolve('x', discover.call);
    cache.remove('x');
    final fresh = cache.resolve('x', discover.call);
    final err = StateError('stale');
    discover.runs[0].completeError(err);
    await expectLater(stale, throwsA(same(err)));

    final joined = cache.resolve('x', discover.call);
    expect(discover.runs, hasLength(2), reason: 'joins the fresh one');
    discover.runs[1].complete(2);
    expect(await fresh, 2);
    expect(await joined, 2);
  });

  test('a discovery that ends after a remove is not cached', () async {
    final stale = cache.resolve('x', discover.call);
    cache.remove('x');
    discover.runs.single.complete(1);
    expect(await stale, 1);

    final next = cache.resolve('x', discover.call);
    expect(discover.runs, hasLength(2), reason: 'a fresh discovery');
    discover.runs[1].complete(2);
    expect(await next, 2);
  });
}
