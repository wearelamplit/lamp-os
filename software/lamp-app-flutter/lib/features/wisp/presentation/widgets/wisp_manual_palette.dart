import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/lamp_card.dart';
import '../../../../core/widgets/settings_row.dart';
import '../../../control/presentation/widgets/color_blocks_bar.dart';
import '../../../control/presentation/widgets/color_stops_sheet.dart';
import '../../application/manual_palette_draft.dart';
import '../../application/wisp_notifier.dart';

class ManualPaletteEditor extends ConsumerWidget {
  const ManualPaletteEditor({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(wispNotifierProvider(lampId).notifier);
    // Watch the status for the loading transition, and the draft so a
    // swatch edit rebuilds the swatch bar.
    ref.watch(wispNotifierProvider(lampId));
    final draft = ref.watch(manualPaletteDraftProvider(lampId));

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

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SettingsGroupHeading('Manual palette'),
        LampCard(
          child: ColorBlocksBar(
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
                // Close the sheet now; the confirm/retry loop runs in the
                // background and surfaces a miss via manualPaletteWriteFailed.
                unawaited(notifier.setManualPalette().catchError((_) {}));
              },
            ),
          ),
        ),
        if (notifier.manualPaletteWriteFailed)
          Padding(
            padding: const EdgeInsets.only(top: AppSpace.sm),
            child: Row(
              children: [
                Icon(Icons.error_outline,
                    size: 16, color: Theme.of(context).colorScheme.error),
                const SizedBox(width: AppSpace.sm),
                Expanded(
                  child: Text(
                    "Palette didn't reach the wisp. It's still showing the old "
                    'colors — try again.',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: Theme.of(context).colorScheme.error,
                        ),
                  ),
                ),
              ],
            ),
          ),
      ],
    );
  }
}
