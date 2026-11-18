import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../domain/tuple_sampler.dart';

/// Picks how lamps take colours from the wisp's palette.
class PaletteStyleSwitch extends StatelessWidget {
  const PaletteStyleSwitch({
    super.key,
    required this.current,
    required this.onSelect,
  });

  final PaletteStyle current;
  final ValueChanged<PaletteStyle> onSelect;

  @override
  Widget build(BuildContext context) {
    final help = switch (current) {
      PaletteStyle.gradient =>
        'Each lamp gets two blends from along the palette.',
      PaletteStyle.swatches =>
        "Each lamp gets two of the palette's own colours, no blends. List a "
            'colour more often and more lamps get it as their base.',
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SectionHeader('Palette style'),
        const SizedBox(height: AppSpace.sm),
        SegmentedButton<PaletteStyle>(
          key: const Key('wisp-palette-style'),
          style: SegmentedButton.styleFrom(
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
          ),
          segments: const [
            ButtonSegment(
              value: PaletteStyle.gradient,
              label: Text('Gradient'),
            ),
            ButtonSegment(
              value: PaletteStyle.swatches,
              label: Text('Swatches'),
            ),
          ],
          selected: {current},
          showSelectedIcon: false,
          onSelectionChanged: (set) {
            if (set.isEmpty) return;
            onSelect(set.first);
          },
        ),
        const SizedBox(height: AppSpace.sm),
        Text(help, style: Theme.of(context).textTheme.bodySmall),
      ],
    );
  }
}
