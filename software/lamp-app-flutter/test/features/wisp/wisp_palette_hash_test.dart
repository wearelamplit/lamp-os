import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';

/// Golden vectors shared byte-for-byte with the wisp native test
/// `test_palette_hash`. The wisp stamps `manualPaletteId()` as its palette id
/// and the app confirms a write by matching the same hash on the status
/// NOTIFY, so both sides must agree on the canonical wire bytes: r,g,b per
/// color, plus w only when w != 0. The strip vector forces the trailing-W
/// strip under [WispRepository.kWispOpPayloadCap] -- the exact path where the
/// two sides could diverge.
void main() {
  group('WispRepository.paletteIdHash golden vectors', () {
    test('W=0 palette hashes r,g,b only', () {
      expect(
        WispRepository.paletteIdHash(const [
          LampColor(r: 1, g: 2, b: 3, w: 0),
          LampColor(r: 4, g: 5, b: 6, w: 0),
        ]),
        '03d252aa',
      );
    });

    test('W-present palette appends w', () {
      expect(
        WispRepository.paletteIdHash(const [
          LampColor(r: 10, g: 20, b: 30, w: 0),
          LampColor(r: 40, g: 50, b: 60, w: 70),
        ]),
        '59a8ae37',
      );
    });

    test('over-cap palette strips trailing W before hashing', () {
      final palette = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      expect(WispRepository.paletteIdHash(palette), '1e98da90');
    });

    test('empty palette is the FNV offset basis', () {
      expect(WispRepository.paletteIdHash(const []), '811c9dc5');
    });
  });
}
