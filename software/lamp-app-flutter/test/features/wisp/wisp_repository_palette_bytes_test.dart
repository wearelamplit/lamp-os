import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';

/// setManualPalette rides a BLE long write to CHAR_WISP_OP, which the lamp's
/// BLE stack caps at 512 B per attribute, and the lamp relays it as
/// MSG_CONTROL_OP. These tests pin the serializer's worst case and the
/// 16-colour cap.
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

    // The widest op the app sends: 16 colours, W on every one, the widest
    // seq. It fits the cap with room to spare, so nothing is ever stripped.
    test('16 all-bright RGBW colours and the widest seq go out whole', () {
      final op = WispRepository.buildManualPaletteOp(bright(16), seq: 65535);
      expect(
        payloadBytes(op),
        lessThanOrEqualTo(WispRepository.kWispOpPayloadCap),
      );
      expect(withW(op), 16);
    });

    test('a palette past 16 colours sends its first 16', () {
      final palette = [
        for (var i = 0; i < 20; i++)
          LampColor(r: i + 1, g: 40 + i, b: 200 - i, w: i.isEven ? 0 : i),
      ];
      final op = WispRepository.buildManualPaletteOp(palette, seq: 9);
      expect(op['colors'], [
        for (final c in palette.take(16))
          c.w == 0 ? [c.r, c.g, c.b] : [c.r, c.g, c.b, c.w],
      ]);
      expect(op['seq'], 9);
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
