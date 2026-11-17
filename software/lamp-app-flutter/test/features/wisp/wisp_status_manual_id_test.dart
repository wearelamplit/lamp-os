import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/wisp/data/wisp_repository.dart';
import 'package:lamp_app/features/wisp/domain/wisp_config.dart';
import 'package:lamp_app/features/wisp/domain/wisp_status.dart';

Uint8List _b(String s) => Uint8List.fromList(utf8.encode(s));

/// The wisp status's `manualId`, and the ids the app computes to compare
/// with it. The vectors match the wisp's `test_palette_hash`.
void main() {
  group('WispStatus.manualId', () {
    test('parses a string, the empty one included', () {
      expect(
        WispStatus.fromBytes(
          _b('{"wispMac":"A","manualId":"abcdef12"}'),
        ).manualId,
        'abcdef12',
      );
      expect(
        WispStatus.fromBytes(_b('{"wispMac":"A","manualId":""}')).manualId,
        '',
      );
    });

    test('is null when absent or not a string', () {
      expect(WispStatus.fromBytes(_b('{"wispMac":"A"}')).manualId, isNull);
      expect(
        WispStatus.fromBytes(_b('{"wispMac":"A","manualId":5}')).manualId,
        isNull,
      );
    });

    test('survives withConfig and copyWith, and counts in equality', () {
      final s = WispStatus.fromBytes(
        _b(
          '{"wispMac":"A","configSplit":true,"source":"osc",'
          '"manualId":"abcdef12"}',
        ),
      );
      const config = WispConfig(
        ledType: 'GRB',
        pixelCount: 30,
        brightness: 100,
        driftIntervalMs: 120000,
        driftFadePct: 50,
        shuffleSeed: 0,
        opSeq: 0,
      );
      expect(s.withConfig(config).manualId, 'abcdef12');
      expect(s.copyWith(brightness: 30).manualId, 'abcdef12');
      final other = WispStatus.fromBytes(
        _b(
          '{"wispMac":"A","configSplit":true,"source":"osc",'
          '"manualId":"12345678"}',
        ),
      );
      expect(s == other, isFalse);
    });
  });

  group('WispRepository manual palette ids', () {
    test('statusManualId is empty for an empty palette', () {
      expect(WispRepository.statusManualId(const []), '');
    });

    test('statusManualId is the palette id otherwise', () {
      expect(
        WispRepository.statusManualId(const [
          LampColor(r: 10, g: 20, b: 30, w: 0),
          LampColor(r: 40, g: 50, b: 60, w: 70),
        ]),
        '59a8ae37',
      );
    });

    test('manualPaletteId hashes the colours as given, with no strip', () {
      final ten = List<LampColor>.filled(
        10,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      expect(WispRepository.manualPaletteId(ten), '999fdcbd');
      final thirty = List<LampColor>.filled(
        30,
        const LampColor(r: 255, g: 255, b: 255, w: 255),
      );
      expect(
        WispRepository.manualPaletteId(thirty),
        isNot(WispRepository.paletteIdHash(thirty)),
      );
    });
  });
}
