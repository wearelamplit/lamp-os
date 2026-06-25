import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/ble_scanner.dart';
import 'package:lamp_app/features/nearby/application/nearby_lamps_notifier.dart';

void main() {
  test('emits scan advertisements and dedupes by id', () async {
    final scanner = FakeBleScanner();
    final container = ProviderContainer(
      overrides: [bleScannerProvider.overrideWithValue(scanner)],
    );
    addTearDown(container.dispose);

    // Subscribe so the notifier starts.
    final sub =
        container.listen(nearbyLampsNotifierProvider, (_, _) {});
    addTearDown(sub.close);

    await Future<void>.delayed(const Duration(milliseconds: 10));
    scanner.emit(const BleAdvertisement(
      id: 'aa',
      name: 'jacko',
      serviceUuids: ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
      baseRgb: 0x300783,
      shadeRgb: 0x000000,
      rssi: -55,
    ));
    await Future<void>.delayed(const Duration(milliseconds: 10));
    expect(container.read(nearbyLampsNotifierProvider).length, 1);

    // Same id — should replace, not duplicate.
    scanner.emit(const BleAdvertisement(
      id: 'aa',
      name: 'jacko',
      serviceUuids: ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
      baseRgb: 0x300783,
      shadeRgb: 0x000000,
      rssi: -50,
    ));
    // 1100 ms > the 1000 ms leading-edge emit window in
    // NearbyLampsNotifier — without this, the second adv lands as
    // pending and the trailing-edge flush hasn't happened yet by the
    // time the test reads state. (M1 audit fix: state emissions are
    // throttled to once per 1000 ms so a 22-lamp fleet doesn't
    // re-notify every consumer at 44 Hz.)
    await Future<void>.delayed(const Duration(milliseconds: 1100));
    final lamps = container.read(nearbyLampsNotifierProvider);
    expect(lamps.length, 1);
    expect(lamps.first.rssi, -50);
  });

  test('sorts roster by RSSI bucket DESC then name ASC for stability',
      () async {
    final scanner = FakeBleScanner();
    final container = ProviderContainer(
      overrides: [bleScannerProvider.overrideWithValue(scanner)],
    );
    addTearDown(container.dispose);
    final sub =
        container.listen(nearbyLampsNotifierProvider, (_, _) {});
    addTearDown(sub.close);
    await Future<void>.delayed(const Duration(milliseconds: 10));

    // Emit four lamps (20 dBm bucket size):
    //   zoey  -65 dBm  bucket -3 (-60..-79)
    //   alpha -68 dBm  bucket -3
    //   bravo -75 dBm  bucket -3
    //   delta -82 dBm  bucket -4 (-80..-99)
    // Expected order after sort: alpha, bravo, zoey, delta. First three
    // are all in bucket -3 → alphabetical; delta alone in bucket -4.
    // Without the sort, "just-heard goes to end" leaves the order as
    // emission order (zoey, alpha, bravo, delta) — alpha would be at
    // position 1 instead of 0, ping-ponging with zoey on every adv.
    for (final (id, name, rssi) in [
      ('z', 'zoey', -65),
      ('a', 'alpha', -68),
      ('b', 'bravo', -75),
      ('d', 'delta', -82),
    ]) {
      scanner.emit(BleAdvertisement(
        id: id,
        name: name,
        serviceUuids: const ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: rssi,
      ));
      await Future<void>.delayed(const Duration(milliseconds: 5));
    }
    // Drain the 1000 ms emit throttle so the final sorted state lands.
    await Future<void>.delayed(const Duration(milliseconds: 1100));
    final lamps = container.read(nearbyLampsNotifierProvider);
    expect(lamps.map((l) => l.name).toList(),
        ['alpha', 'bravo', 'zoey', 'delta']);
  });

  test('roster sort survives ping-pong: same two lamps re-adverting '
      'in alternating order stay sorted', () async {
    final scanner = FakeBleScanner();
    final container = ProviderContainer(
      overrides: [bleScannerProvider.overrideWithValue(scanner)],
    );
    addTearDown(container.dispose);
    final sub =
        container.listen(nearbyLampsNotifierProvider, (_, _) {});
    addTearDown(sub.close);
    await Future<void>.delayed(const Duration(milliseconds: 10));

    // Two lamps in the same RSSI bucket — exactly the case the user
    // reported as bouncing around in the social tab. Each adv used to
    // append the just-heard lamp to the end. Now: alphabetical by
    // name within the bucket regardless of arrival order.
    for (var i = 0; i < 5; i++) {
      scanner.emit(const BleAdvertisement(
        id: 'b',
        name: 'bravo',
        serviceUuids: ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: -65,
      ));
      await Future<void>.delayed(const Duration(milliseconds: 5));
      scanner.emit(const BleAdvertisement(
        id: 'a',
        name: 'alpha',
        serviceUuids: ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: -67,
      ));
      await Future<void>.delayed(const Duration(milliseconds: 5));
    }
    await Future<void>.delayed(const Duration(milliseconds: 1100));
    final lamps = container.read(nearbyLampsNotifierProvider);
    expect(lamps.map((l) => l.name).toList(), ['alpha', 'bravo']);
  });

  test('20 dBm buckets keep same-room peers stably alphabetical when one '
      'drifts across the OLD 10 dBm boundary', () async {
    // Anti-jitter regression: at the previous 10 dBm bucket size, a
    // peer drifting between -65 and -78 dBm would hop bucket -6/-7 and
    // swap position with an in-room peer in adjacent buckets. The 20
    // dBm widening collapses -60..-79 into one bucket, so the same
    // jitter no longer flips order — name takes over within the band.
    //
    // Setup: alpha at -65 dBm (bucket -3 under 20 dBm), zoey at -73
    // dBm (would have been bucket -7 under 10 dBm, now -3 alongside
    // alpha). Cycle: zoey re-advs at -78 (still bucket -3 at 20 dBm,
    // would have been -7 at 10 dBm), alpha at -67 (always bucket -3).
    // Final order MUST remain alphabetical: [alpha, zoey].
    final scanner = FakeBleScanner();
    final container = ProviderContainer(
      overrides: [bleScannerProvider.overrideWithValue(scanner)],
    );
    addTearDown(container.dispose);
    final sub =
        container.listen(nearbyLampsNotifierProvider, (_, _) {});
    addTearDown(sub.close);
    await Future<void>.delayed(const Duration(milliseconds: 10));

    for (var i = 0; i < 5; i++) {
      scanner.emit(BleAdvertisement(
        id: 'a',
        name: 'alpha',
        serviceUuids: const ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: i.isEven ? -65 : -67,
      ));
      await Future<void>.delayed(const Duration(milliseconds: 5));
      scanner.emit(BleAdvertisement(
        id: 'z',
        name: 'zoey',
        serviceUuids: const ['5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40'],
        baseRgb: 0x300783,
        shadeRgb: 0x000000,
        rssi: i.isEven ? -73 : -78,
      ));
      await Future<void>.delayed(const Duration(milliseconds: 5));
    }
    await Future<void>.delayed(const Duration(milliseconds: 1100));
    final lamps = container.read(nearbyLampsNotifierProvider);
    expect(lamps.map((l) => l.name).toList(), ['alpha', 'zoey']);
  });
}
