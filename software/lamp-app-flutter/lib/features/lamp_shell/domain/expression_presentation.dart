import 'package:flutter/material.dart';

/// Client-side presentation for an expression type: the icon and the picker
/// tagline. These are UX choices, not schema. The firmware `exprcat` catalog
/// owns everything structural (params, ranges, types, names, and the
/// wisp-pause flag). Keyed by the catalog `id`.
class ExpressionPresentation {
  const ExpressionPresentation({
    required this.icon,
    required this.tagline,
  });

  final IconData icon;
  final String tagline;

  static const _byId = <String, ExpressionPresentation>{
    'breathing': ExpressionPresentation(
      icon: Icons.air,
      tagline: 'A gentle, continuous breath between palette colors.',
    ),
    'pulse': ExpressionPresentation(
      icon: Icons.graphic_eq,
      tagline: 'A wave of color that sweeps across the strip.',
    ),
    'shifty': ExpressionPresentation(
      icon: Icons.shuffle,
      tagline: 'Slow ambient drift toward random palette colors.',
    ),
    'glitchy': ExpressionPresentation(
      icon: Icons.bolt,
      tagline: 'Rare, sudden flickers of a random palette color.',
    ),
    'spotty': ExpressionPresentation(
      icon: Icons.blur_on,
      tagline: 'Random blinking points.',
    ),
  };

  static const fallback =
      ExpressionPresentation(icon: Icons.auto_awesome, tagline: '');

  static ExpressionPresentation forId(String id) => _byId[id] ?? fallback;
}
