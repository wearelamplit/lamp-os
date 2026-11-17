import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';

/// setManualPalette rides a BLE long write to CHAR_WISP_OP, which the lamp's
/// BLE stack caps at 512 B per attribute, and the lamp relays it as
/// MSG_CONTROL_OP. These tests pin the serializer's worst case.
void main() {
  int payloadBytes(Map<String, dynamic> op) =>
      utf8.encode(jsonEncode(op)).length;

  List<LampColor> bright(int n) => List<LampColor>.filled(
        n,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );

  int withW(Map<String, dynamic> op) =>
      (op['colors'] as List).where((c) => (c as List).length == 4).length;

  group('WispRepository.buildManualPaletteOp byte budget', () {
    // The editor's largest palette: 10 colours, W on every one.
    test('10-color all-bright RGBW goes out whole, every W kept', () {
      final op = WispRepository.buildManualPaletteOp(bright(10));
      expect(payloadBytes(op), 232);
      expect(withW(op), 10);
      for (final c in op['colors'] as List) {
        expect(c, [255, 255, 255, 255]);
      }
    });

    // 25 W-heavy colours are 502 B, 26 are 520 B: the strip first fires
    // there, and only on trailing colours, never on RGB.
    test('the W strip starts at the 26th colour, from the tail', () {
      final op25 = WispRepository.buildManualPaletteOp(bright(25));
      expect(withW(op25), 25);

      final op26 = WispRepository.buildManualPaletteOp(bright(26));
      expect(
        payloadBytes(op26),
        lessThanOrEqualTo(WispRepository.kWispOpPayloadCap),
      );
      final colors = op26['colors'] as List;
      expect(colors.length, 26);
      expect(withW(op26), lessThan(26));
      for (final c in colors) {
        expect((c as List).sublist(0, 3), [255, 255, 255]);
      }
      expect((colors.first as List).length, 4);
      expect((colors.last as List).length, 3);
    });

    // One byte either side of the cap: a 512 B op keeps every W, a 513 B
    // one loses the last W.
    test('the cap is exactly 512 B', () {
      final at512 = WispRepository.buildManualPaletteOp(
        [...bright(25), const LampColor(r: 1, g: 1, b: 1, w: 1)],
      );
      expect(payloadBytes(at512), 512);
      expect(withW(at512), 26);

      final at513 = WispRepository.buildManualPaletteOp(
        [...bright(25), const LampColor(r: 10, g: 1, b: 1, w: 1)],
      );
      expect(payloadBytes(at513), 511);
      expect(withW(at513), 25);
      expect((at513['colors'] as List).last, [10, 1, 1]);
    });

    test('10-color all-bright RGB (w == 0) emits triples under the cap', () {
      final palette = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 0),
      );
      final op = WispRepository.buildManualPaletteOp(palette);
      expect(
        payloadBytes(op),
        lessThanOrEqualTo(WispRepository.kWispOpPayloadCap),
      );
      for (final c in op['colors'] as List) {
        expect((c as List).length, 3);
      }
    });

    test('mixed palette keeps [r,g,b,w] for w > 0 when under the cap', () {
      final op = WispRepository.buildManualPaletteOp(const [
        LampColor(r: 1, g: 2, b: 3, w: 0),
        LampColor(r: 4, g: 5, b: 6, w: 7),
      ]);
      expect(op['colors'], [
        [1, 2, 3],
        [4, 5, 6, 7],
      ]);
    });
  });
}
