import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';

/// setManualPalette rides a BLE write that the lamp relays as
/// MSG_CONTROL_OP; a payload over CONTROL_MAX_PAYLOAD (230 B) is silently
/// dropped at the relay. These tests pin the serializer's worst case.
void main() {
  int payloadBytes(Map<String, dynamic> op) =>
      utf8.encode(jsonEncode(op)).length;

  group('WispRepository.buildManualPaletteOp byte budget', () {
    test('10-color all-bright RGBW stays under the 230 B relay cap', () {
      final palette = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      final op = WispRepository.buildManualPaletteOp(palette);
      expect(
        payloadBytes(op),
        lessThanOrEqualTo(WispRepository.kWispOpPayloadCap),
      );
      final colors = op['colors'] as List;
      expect(colors.length, 10);
      // Cap pressure may strip W from trailing colors, never RGB.
      for (final c in colors) {
        final tuple = c as List;
        expect(tuple.length, anyOf(3, 4));
        expect(tuple.sublist(0, 3), [255, 255, 255]);
      }
      // At most the last color loses its W in this worst case.
      expect((colors[0] as List).length, 4);
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
