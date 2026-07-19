import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../../control/presentation/widgets/color_blocks_bar.dart';
import '../../../control/presentation/widgets/color_stops_sheet.dart';
import '../../application/wisp_notifier.dart';

class ManualPaletteEditor extends ConsumerWidget {
  const ManualPaletteEditor({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(wispNotifierProvider(lampId).notifier);
    // Watch so the editor rebuilds when the notifier emits draft / save
    // state changes via _bumpState.
    ref.watch(wispNotifierProvider(lampId));

    if (notifier.paletteLoading) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: AppSpace.md),
        child: Row(
          children: [
            const SizedBox(
              width: 14, height: 14, // deliberate dimension, not spacing
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
            const SizedBox(width: AppSpace.md),
            Text('Reading palette from wisp…',
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      fontStyle: FontStyle.italic,
                    )),
          ],
        ),
      );
    }

    final draft = notifier.draftManualPalette;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Manual palette'),
        const SizedBox(height: AppSpace.sm),
        ColorBlocksBar(
          colors: draft,
          onTap: () => showColorStopsSheet(
            context,
            initial: draft,
            title: 'Manual palette',
            description: 'The wisp paints from these colors.',
            max: 10,
            allowEmpty: true,
            reorderable: false,
            onChanged: notifier.setManualPaletteDraft,
            onSave: (colors) async {
              notifier.setManualPaletteDraft(colors);
              await notifier.setManualPalette();
            },
          ),
        ),
      ],
    );
  }
}
