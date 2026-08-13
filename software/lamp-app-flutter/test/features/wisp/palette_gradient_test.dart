import 'dart:ui' show Color;

import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/wisp/domain/palette_gradient.dart';

/// Tests for the palette-ramp helper. Results must stay in sync with the
/// firmware's StatusRing gradient math; divergence means the on-screen
/// bar drifts from the LED ring. Shape tests use 30 pixels to match
/// `wisp::kStatusRingPixelCount`.

void main() {
  // Channel-extraction helpers. Flutter 3.27+ exposes `.r/.g/.b` as
  // doubles in [0,1]; multiply back to 0..255 and round for byte-level
  // assertions. Centralised here so test bodies stay legible.
  int r(Color c) => (c.r * 255).round();
  int g(Color c) => (c.g * 255).round();
  int b(Color c) => (c.b * 255).round();

  group('renderPaletteRamp', () {
    test('empty palette falls back to warm white on every pixel', () {
      final ramp = renderPaletteRamp(const <Color>[], 30);
      expect(ramp.length, 30);
      for (final px in ramp) {
        expect(r(px), 255);
        expect(g(px), 180);
        expect(b(px), 100);
      }
    });

    test('single stop fills uniformly', () {
      final stops = <Color>[const Color.fromARGB(0xFF, 12, 34, 56)];
      final ramp = renderPaletteRamp(stops, 30);
      expect(ramp.length, 30);
      for (final px in ramp) {
        expect(r(px), 12);
        expect(g(px), 34);
        expect(b(px), 56);
      }
    });

    test('two stops anchor exactly at the endpoints', () {
      // Black → white. Pixel 0 must be pure black, pixel 29 pure white.
      final stops = <Color>[
        const Color.fromARGB(0xFF, 0, 0, 0),
        const Color.fromARGB(0xFF, 255, 255, 255),
      ];
      final ramp = renderPaletteRamp(stops, 30);
      expect(r(ramp.first), 0);
      expect(g(ramp.first), 0);
      expect(b(ramp.first), 0);
      expect(r(ramp.last), 255);
      expect(g(ramp.last), 255);
      expect(b(ramp.last), 255);
    });

    test('two stops produce a symmetric black↔white ramp', () {
      // Opposite pixels must sum to 255 within 1 LSB rounding tolerance per channel.
      final stops = <Color>[
        const Color.fromARGB(0xFF, 0, 0, 0),
        const Color.fromARGB(0xFF, 255, 255, 255),
      ];
      final ramp = renderPaletteRamp(stops, 30);
      for (var i = 0; i < 15; i++) {
        final low = ramp[i];
        final high = ramp[29 - i];
        for (final ch in <int Function(Color)>[r, g, b]) {
          final sum = ch(low) + ch(high);
          expect(
            sum,
            inInclusiveRange(254, 256),
            reason: 'channel sum at index $i',
          );
        }
      }
    });

    test('two-stop red→blue ramp is monotonic per channel', () {
      // Red drops monotonically, blue rises monotonically, green stays 0.
      final stops = <Color>[
        const Color.fromARGB(0xFF, 255, 0, 0),
        const Color.fromARGB(0xFF, 0, 0, 255),
      ];
      final ramp = renderPaletteRamp(stops, 30);
      for (var i = 1; i < ramp.length; i++) {
        expect(
          r(ramp[i]) <= r(ramp[i - 1]),
          isTrue,
          reason: 'red non-increasing at $i',
        );
        expect(
          b(ramp[i]) >= b(ramp[i - 1]),
          isTrue,
          reason: 'blue non-decreasing at $i',
        );
        expect(g(ramp[i]), 0);
      }
    });

    test(
      'three stops anchor at endpoints + peak lands in the middle third',
      () {
        // R, G, B at positions 0, 0.5, 1.0 over 30 pixels. Pixel 0 should
        // be pure red, pixel 29 pure blue, and the green peak should land
        // in pixels 10..19 (matching the C++ test's tolerance — with 30
        // pixels the middle stop straddles pixels 14/15 rather than hitting
        // 255 exactly).
        final stops = <Color>[
          const Color.fromARGB(0xFF, 255, 0, 0),
          const Color.fromARGB(0xFF, 0, 255, 0),
          const Color.fromARGB(0xFF, 0, 0, 255),
        ];
        final ramp = renderPaletteRamp(stops, 30);
        expect(r(ramp.first), 255);
        expect(g(ramp.first), 0);
        expect(b(ramp.first), 0);
        expect(r(ramp.last), 0);
        expect(g(ramp.last), 0);
        expect(b(ramp.last), 255);

        var peakIdx = 0;
        var peakG = 0;
        for (var i = 0; i < ramp.length; i++) {
          if (g(ramp[i]) > peakG) {
            peakG = g(ramp[i]);
            peakIdx = i;
          }
        }
        expect(peakG, greaterThanOrEqualTo(240));
        expect(peakIdx, inInclusiveRange(10, 19));
      },
    );

    test('ten stops endpoint exact + no overrun', () {
      // Same fixture as `test_ten_stops_no_buffer_overrun` in the firmware
      // tests: 10 stops on 30 pixels, endpoints must land exactly on
      // stops[0] and stops[9].
      final stops = <Color>[
        for (var i = 0; i < 10; i++)
          Color.fromARGB(0xFF, i * 25, 255 - i * 25, i * 10),
      ];
      final ramp = renderPaletteRamp(stops, 30);
      expect(r(ramp.first), 0);
      expect(g(ramp.first), 255);
      expect(b(ramp.first), 0);
      expect(r(ramp.last), 9 * 25);
      expect(g(ramp.last), 255 - 9 * 25);
      expect(b(ramp.last), 9 * 10);
    });

    test('pixelCount=0 returns an empty list', () {
      expect(renderPaletteRamp(const <Color>[], 0), isEmpty);
      expect(renderPaletteRamp(<Color>[const Color(0xFFFFFFFF)], 0), isEmpty);
    });

    test('pixelCount=1 collapses to the first stop', () {
      final stops = <Color>[
        const Color.fromARGB(0xFF, 10, 20, 30),
        const Color.fromARGB(0xFF, 200, 210, 220),
      ];
      final ramp = renderPaletteRamp(stops, 1);
      expect(ramp.length, 1);
      expect(r(ramp.first), 10);
    });

    test('256-px render still anchors at endpoints', () {
      // The on-screen bar samples at much higher resolution than the
      // 30-px LED ring; check that the math degrades cleanly. With 256
      // pixels and two stops, pixel 0 = stops[0] and pixel 255 = stops[1].
      final stops = <Color>[
        const Color.fromARGB(0xFF, 30, 60, 90),
        const Color.fromARGB(0xFF, 200, 100, 50),
      ];
      final ramp = renderPaletteRamp(stops, 256);
      expect(ramp.length, 256);
      expect(r(ramp.first), 30);
      expect(g(ramp.first), 60);
      expect(b(ramp.first), 90);
      expect(r(ramp.last), 200);
      expect(g(ramp.last), 100);
      expect(b(ramp.last), 50);
    });
  });

  group('foldRgbwWarmBias', () {
    test('W=0 → RGB passthrough', () {
      final out = foldRgbwWarmBias(100, 50, 200, 0);
      expect(r(out), 100);
      expect(g(out), 50);
      expect(b(out), 200);
    });

    test('W=100 → +70 R, +40 G, B unchanged', () {
      final out = foldRgbwWarmBias(10, 20, 30, 100);
      expect(r(out), 80);
      expect(g(out), 60);
      expect(b(out), 30);
    });

    test('W=255 clamps R and G to 255 without touching B', () {
      final out = foldRgbwWarmBias(250, 240, 50, 255);
      expect(r(out), 255);
      expect(g(out), 255);
      expect(b(out), 50);
    });
  });

  group('kWarmWhite', () {
    test('matches the StatusRing warm-white constant', () {
      // The constant must stay byte-for-byte in sync with
      // `kWarmWhiteR/G/B` in StatusRing.h or the empty-palette fallback
      // on screen will drift from the LED ring.
      expect(r(kWarmWhite), 255);
      expect(g(kWarmWhite), 180);
      expect(b(kWarmWhite), 100);
    });
  });
}
