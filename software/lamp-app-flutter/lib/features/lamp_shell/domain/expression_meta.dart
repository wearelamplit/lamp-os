import 'package:flutter/material.dart';

/// Descriptive metadata for the four expression types the firmware ships.
/// Pulled from the C++ headers under `software/lamp-os/src/expressions/`.
class ExpressionTypeMeta {
  const ExpressionTypeMeta({
    required this.key,
    required this.name,
    required this.icon,
    required this.tagline,
    required this.description,
    required this.defaultParameters,
    this.defaultDisabledDuringWispOverride = false,
    this.isContinuous = false,
  });

  /// Wire-level identifier the firmware switches on (`expression.type`).
  final String key;

  /// Human-friendly label shown in the picker and editor header.
  final String name;

  /// Single-character glyph for the picker tile.
  final IconData icon;

  /// One-line description for the picker card. Keep under ~80 chars.
  final String tagline;

  /// Longer explainer rendered at the bottom of the expression editor so
  /// users can see, in plain language, what each slider in this type
  /// actually controls. Tagline is the punchy card-line; description is
  /// the in-editor cheat sheet.
  final String description;

  /// Parameter defaults that match the firmware's hard-coded defaults so a
  /// freshly-created expression behaves the same as a no-parameters version.
  /// All values are `uint32_t` on the firmware side.
  final Map<String, int> defaultParameters;

  /// Default for the `disabledDuringWispOverride` flag on a freshly-
  /// created expression of this type. True for expressions that paint
  /// continuously and visibly fight the wisp's hold colour (breathing,
  /// shifty); false for brief / discrete expressions that coexist
  /// nicely with a held colour (glitchy, pulse). Operator can flip the
  /// flag per-expression in the editor. See `docs/dev/expressions.md`.
  final bool defaultDisabledDuringWispOverride;

  /// True for expressions that run until explicitly stopped (breathing,
  /// shifty). The editor uses this to render a Stop affordance while
  /// previewActive is true; one-shot expressions just show a brief
  /// "Testing…" before the firmware-driven previewActive flips back.
  final bool isContinuous;

  /// Ordered for picker display.
  static const all = <ExpressionTypeMeta>[
    ExpressionTypeMeta(
      key: 'breathing',
      name: 'Breathing',
      icon: Icons.air,
      tagline: 'A gentle, continuous breath between palette colors.',
      description:
          'A slow, continuous inhale/exhale across the chosen colors. '
          'Breath cycle length sets the tempo.',
      defaultParameters: {'breathSpeed': 10},
      defaultDisabledDuringWispOverride: true,
    ),
    ExpressionTypeMeta(
      key: 'pulse',
      name: 'Pulse',
      icon: Icons.graphic_eq,
      tagline: 'A wave of color that sweeps across the strip.',
      description:
          'A band of color that travels along the lamp, blending with '
          'the colors already showing. Pulse speed sets how fast it '
          'sweeps from end to end.',
      defaultParameters: {
        'pulseSpeed': 3,
        // Mesh-trigger cascade convention (firmware-side, generic across
        // expressions): when cascadeEnabled is 1, the lamp fans out a
        // matching invocation to every reachable peer on local trigger,
        // staggered by cascadeStaggerMs between successive lamps.
        'cascadeEnabled': 0,
        'cascadeStaggerMs': 0,
      },
    ),
    ExpressionTypeMeta(
      key: 'shifty',
      name: 'Shifty',
      icon: Icons.shuffle,
      tagline: 'Slow ambient drift toward random palette colors.',
      description:
          'Slowly fades to a chosen color, holds there, then fades '
          'back. Hold time is the dwell at peak; fade duration is the '
          'crossfade in and out.',
      defaultParameters: {
        'shiftDurationMin': 300,
        'shiftDurationMax': 600,
        'fadeDuration': 60,
      },
      defaultDisabledDuringWispOverride: true,
    ),
    ExpressionTypeMeta(
      key: 'glitchy',
      name: 'Glitchy',
      icon: Icons.bolt,
      tagline: 'Rare, sudden flickers of a random palette color.',
      description:
          'A brief stutter that flashes through the chosen colors. '
          'Frequency sets how often, predictability tightens or '
          'loosens the timing, and glitch duration controls how long '
          'each flash holds.',
      defaultParameters: {
        'durationMin': 1,
        'durationMax': 3,
        // Mesh-trigger cascade convention (firmware-side, generic across
        // expressions): when cascadeEnabled is 1, the lamp fans out a
        // matching invocation to every reachable peer on local trigger,
        // staggered by cascadeStaggerMs between successive lamps.
        'cascadeEnabled': 0,
        'cascadeStaggerMs': 0,
      },
    ),
  ];

  static ExpressionTypeMeta? byKey(String key) {
    for (final m in all) {
      if (m.key == key) return m;
    }
    return null;
  }
}

/// User-facing label for the three [ExpressionTarget] values.
String targetLabel(int target) => switch (target) {
      1 => 'Shade',
      2 => 'Base',
      3 => 'Both',
      _ => 'Both',
    };
