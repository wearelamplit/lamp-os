import 'dart:math';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/ble/reaching_lamp_lines.dart';

void main() {
  test('there are 10 state-neutral line templates, each with {name}', () {
    expect(reachingLampLines.length, 10);
    for (final t in reachingLampLines) {
      expect(t.contains('{name}'), isTrue, reason: t);
    }
  });

  test('reachingLampLine interpolates the lamp name', () {
    expect(reachingLampLine('Reaching out to {name}…', 'gramp'),
        'Reaching out to gramp…');
  });

  test('ShuffleBag emits every item once per cycle before repeating', () {
    final items = List.generate(10, (i) => i);
    final bag = ShuffleBag<int>(items, Random(1));
    for (var cycle = 0; cycle < 3; cycle++) {
      final seen = <int>{};
      for (var i = 0; i < items.length; i++) {
        seen.add(bag.next());
      }
      expect(seen, items.toSet(), reason: 'cycle $cycle missed or repeated');
    }
  });
}
