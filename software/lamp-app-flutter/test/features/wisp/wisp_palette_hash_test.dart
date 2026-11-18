import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';

/// Golden vectors shared byte-for-byte with the wisp native test
/// `test_palette_hash`. The wisp states `manualPaletteId()` as its palette's
/// `manualId`, and the app matches the same hash against palette reads and
/// save echoes, so both sides must agree on the canonical wire bytes: r,g,b
/// per color, plus w only when w != 0. The 16-colour vector is the widest
/// palette the app sends and the wisp stores.
const _sixteenBright = 'bbc5c685';

void main() {
  group('WispRepository palette id golden vectors', () {
    test('W=0 palette hashes r,g,b only', () {
      expect(
        WispRepository.manualPaletteId(const [
          LampColor(r: 1, g: 2, b: 3, w: 0),
          LampColor(r: 4, g: 5, b: 6, w: 0),
        ]),
        '03d252aa',
      );
    });

    test('W-present palette appends w', () {
      expect(
        WispRepository.manualPaletteId(const [
          LampColor(r: 10, g: 20, b: 30, w: 0),
          LampColor(r: 40, g: 50, b: 60, w: 70),
        ]),
        '59a8ae37',
      );
    });

    test('10 colours with W on each keep every W', () {
      final palette = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      expect(WispRepository.manualPaletteId(palette), '999fdcbd');
    });

    // The app sends and the wisp stores the first 16, so both hash those.
    test('a palette past 16 colours hashes its first 16', () {
      final palette = List<LampColor>.filled(
        20,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      expect(WispRepository.sentManualId(palette), _sixteenBright);
      expect(
        WispRepository.manualPaletteId(palette.take(16).toList()),
        _sixteenBright,
      );
    });

    test('empty palette is the FNV offset basis', () {
      expect(WispRepository.manualPaletteId(const []), '811c9dc5');
    });
  });
}
