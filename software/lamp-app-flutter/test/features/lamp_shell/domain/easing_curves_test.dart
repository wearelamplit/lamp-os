import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/lamp_shell/domain/easing_curves.dart';
import 'package:lamp_app/features/lamp_shell/domain/expression_catalog.dart';

const _catalog = [
  EnumOption(value: 0, label: 'Linear', group: 'Linear'),
  EnumOption(value: 4, label: 'In', group: 'Smooth'),
  EnumOption(value: 3, label: 'Out', group: 'Smooth'),
  EnumOption(value: 1, label: 'In-out', group: 'Smooth'),
  EnumOption(value: 8, label: 'In', group: 'Overshoot'),
  EnumOption(value: 9, label: 'Out', group: 'Overshoot'),
  EnumOption(value: 10, label: 'In-out', group: 'Overshoot'),
  EnumOption(value: 17, label: 'Random', group: 'Random'),
];

const _legacyCatalog = [
  EnumOption(value: 0, label: 'Linear'),
  EnumOption(value: 1, label: 'Smooth'),
  EnumOption(value: 3, label: 'Settle'),
];

void main() {
  test('new curves hit endpoints', () {
    for (final v in [5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]) {
      expect(applyEasing(v, 0.0), closeTo(0.0, 1e-4));
      expect(applyEasing(v, 1.0), closeTo(1.0, 1e-4));
    }
  });

  test('In is reflection of Out', () {
    for (var t = 0.0; t <= 1.0; t += 0.1) {
      expect(applyEasing(5, t), closeTo(1.0 - applyEasing(6, 1.0 - t), 1e-4));
    }
  });

  test('overshoot can exceed unit range mid-curve', () {
    var over = false;
    for (var t = 0.5; t < 1.0; t += 0.02) {
      if (applyEasing(9, t) > 1.0) over = true;
    }
    expect(over, isTrue);
  });

  test('existing curves 0-4 unchanged', () {
    expect(applyEasing(0, 0.5), 0.5);
    expect(applyEasing(1, 0.5), closeTo(0.5, 1e-9));
    expect(applyEasing(3, 0.5), closeTo(0.75, 1e-9));
    expect(applyEasing(4, 0.5), closeTo(0.25, 1e-9));
  });

  test('blurbs cover every motion family', () {
    for (final family in [
      'Linear',
      'Smooth',
      'Float',
      'Snap',
      'Overshoot',
      'Spring',
      'Bounce',
      'Random',
    ]) {
      expect(easingBlurbs.containsKey(family), isTrue, reason: family);
    }
  });

  test('groupEasing yields families in catalog order', () {
    final families = groupEasing(_catalog);
    expect(families.map((f) => f.family), [
      'Linear',
      'Smooth',
      'Overshoot',
      'Random',
    ]);
    expect(families.firstWhere((f) => f.family == 'Smooth').directions.map((d) => d.label),
        ['In', 'Out', 'In-out']);
  });

  test('resolveEasing looks up value by family + direction', () {
    expect(resolveEasing(_catalog, 'Smooth', 'Out'), 3);
    expect(resolveEasing(_catalog, 'Overshoot', 'In-out'), 10);
  });

  test('resolveEasing on a singleton family ignores direction', () {
    expect(resolveEasing(_catalog, 'Random', 'In'), 17);
    expect(resolveEasing(_catalog, 'Random', 'Out'), 17);
  });

  test('resolveEasing returns null for an absent family', () {
    expect(resolveEasing(_catalog, 'Nope', 'In'), isNull);
  });

  test('describeEasing reverse-looks-up family + direction for a value', () {
    expect(describeEasing(_catalog, 3), (family: 'Smooth', direction: 'Out'));
    expect(describeEasing(_catalog, 17), (family: 'Random', direction: 'Random'));
  });

  test('a legacy catalog with no group yields one family per option, never throws', () {
    final families = groupEasing(_legacyCatalog);
    expect(families.map((f) => f.family), ['Linear', 'Smooth', 'Settle']);
    expect(resolveEasing(_legacyCatalog, 'Settle', 'anything'), 3);
    expect(describeEasing(_legacyCatalog, 1), (family: 'Smooth', direction: 'Smooth'));
  });
}
