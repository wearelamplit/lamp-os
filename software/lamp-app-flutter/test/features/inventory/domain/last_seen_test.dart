import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/inventory/domain/last_seen.dart';

void main() {
  // Pick a fixed reference moment so tests are deterministic.
  final now = DateTime.fromMillisecondsSinceEpoch(1748520000000);
  final base = now.millisecondsSinceEpoch;

  test('< 60s → Just now', () {
    expect(formatLastSeen(base - 5000, now), 'Just now');
  });
  test('5 minutes', () {
    expect(formatLastSeen(base - 5 * 60000, now), '5m ago');
  });
  test('2 hours', () {
    expect(formatLastSeen(base - 2 * 3600000, now), '2h ago');
  });
  test('3 days', () {
    expect(formatLastSeen(base - 3 * 86400000, now), '3d ago');
  });
  test('2 weeks (14 days exactly)', () {
    expect(formatLastSeen(base - 14 * 86400000, now), '2 weeks ago');
  });
  test('5 months (~150 days)', () {
    expect(formatLastSeen(base - 150 * 86400000, now), '5 months ago');
  });
  test('over a year', () {
    expect(formatLastSeen(base - 400 * 86400000, now), 'over a year ago');
  });
  test('clock skew (future timestamp)', () {
    expect(formatLastSeen(base + 1000, now), 'Just now');
  });
  test('boundary: 1 minute', () {
    expect(formatLastSeen(base - 60000, now), '1m ago');
  });
  test('boundary: 1 day', () {
    expect(formatLastSeen(base - 86400000, now), '1d ago');
  });
  test('boundary: 12 months (~360 days)', () {
    expect(formatLastSeen(base - 360 * 86400000, now), 'over a year ago');
  });
}
