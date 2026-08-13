import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:lamp_app/features/inventory/presentation/my_lamps_screen.dart';

InventoryLamp _lamp(String id, String name) => InventoryLamp(id: id, name: name);

void main() {
  group('sortMyLamps', () {
    test('alphabetical, case-insensitive', () {
      final out = sortMyLamps(
        [_lamp('1', 'zed'), _lamp('2', 'Alpha'), _lamp('3', 'beta')],
        null,
      );
      expect(out.map((l) => l.name), ['Alpha', 'beta', 'zed']);
    });

    test('active lamp pins to the top regardless of name', () {
      final out = sortMyLamps(
        [_lamp('1', 'Alpha'), _lamp('2', 'zed'), _lamp('3', 'beta')],
        '2',
      );
      expect(out.map((l) => l.id), ['2', '1', '3']);
    });

    test('equal names tiebreak on id, stably', () {
      final out = sortMyLamps(
        [_lamp('b', 'Lamp'), _lamp('a', 'lamp'), _lamp('c', 'LAMP')],
        null,
      );
      expect(out.map((l) => l.id), ['a', 'b', 'c']);
    });
  });
}
