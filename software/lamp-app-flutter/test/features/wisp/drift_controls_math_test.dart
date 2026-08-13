import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/presentation/widgets/drift_controls.dart';

void main() {
  group('drift mapping', () {
    test('posToMs(0.0) == 30000', () => expect(posToMs(0.0), 30000));
    test('posToMs(1.0) == 3600000', () => expect(posToMs(1.0), 3600000));
    test('msToPos(120000) ≈ 0.29', () {
      expect(msToPos(120000), closeTo(0.2895, 1e-3));
    });
    test('round-trip within ±1ms', () {
      for (final p in [0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0]) {
        final ms = posToMs(p);
        final p2 = msToPos(ms);
        expect(
          posToMs(p2),
          closeTo(ms.toDouble(), 1.0),
          reason: 'round-trip failed for p=$p (ms=$ms)',
        );
      }
    });
  });
}
