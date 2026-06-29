import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/color_picker_sheet.dart';
import '../../../control/presentation/widgets/lamp_color_swatch.dart';
import '../../application/wisp_notifier.dart';

class ManualPaletteEditor extends ConsumerStatefulWidget {
  const ManualPaletteEditor({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<ManualPaletteEditor> createState() =>
      _ManualPaletteEditorState();
}

class _ManualPaletteEditorState extends ConsumerState<ManualPaletteEditor> {
  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // Watch so the editor rebuilds when the notifier emits draft / save
    // state changes via _bumpState.
    ref.watch(wispNotifierProvider(widget.lampId));

    if (notifier.paletteLoading) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: AppSpace.md),
        child: Row(
          children: [
            const SizedBox(
              width: 14, height: 14,
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
            const SizedBox(width: 10),
            Text('Reading palette from wisp…',
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  fontStyle: FontStyle.italic,
                )),
          ],
        ),
      );
    }

    final draft = notifier.draftManualPalette;
    final atCap = draft.length >= 10;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Manual palette'),
        const SizedBox(height: AppSpace.sm),
        Wrap(
          spacing: AppSpace.sm,
          runSpacing: AppSpace.sm,
          children: [
            for (var i = 0; i < draft.length; i++)
              _WispColorChip(
                color: draft[i],
                onEdit: () => _editAt(i),
                onRemove: () => notifier.removeManualPaletteColor(i),
              ),
            if (!atCap)
              TextButton.icon(
                icon: const Icon(Icons.add, size: 18),
                label: const Text('Add color'),
                onPressed: _addNew,
              ),
          ],
        ),
        if (atCap) ...[
          const SizedBox(height: 4),
          Text(
            'Palette is at the 10-color cap. Remove a swatch to add another.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
              fontStyle: FontStyle.italic,
            ),
          ),
        ],
      ],
    );
  }

  Future<void> _addNew() async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // Default new swatch — pure white (RGB=255). Hue-saturated would feel
    // editorial; white reads as "blank slate, pick me".
    const initial = LampColor(r: 255, g: 255, b: 255, w: 0);
    final picked = await showColorPickerSheet(
      context,
      initial: initial,
      title: 'Add palette color',
      bpp: 3, // RGB only — wisp manual palette has no W channel.
    );
    if (picked == null) return;
    notifier.appendManualPaletteColor(picked);
  }

  Future<void> _editAt(int index) async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    final draft = notifier.draftManualPalette;
    if (index < 0 || index >= draft.length) return;
    // onLive streams the picker's in-progress color into the notifier on
    // every drag tick. The notifier debounces the BLE write internally so
    // the wisp doesn't get saturated; the gradient bar at the top updates
    // immediately. If the user cancels, the picker returns null and we
    // restore the original colour (the wisp will get one trailing write
    // with the restored value).
    final original = draft[index];
    final picked = await showColorPickerSheet(
      context,
      initial: original,
      title: 'Edit palette color',
      bpp: 3,
      onLive: (live) => notifier.updateManualPaletteColor(index, live),
    );
    if (picked == null) {
      notifier.updateManualPaletteColor(index, original);
    } else {
      notifier.updateManualPaletteColor(index, picked);
    }
  }
}

/// Expressions-style swatch chip: tap to edit, top-right X to remove.
/// Distinct from the editor's `_ColorChip` only in that the X is always
/// shown (the wisp's manual palette is allowed to be empty — the wisp
/// falls back to warm white on the ring). Visual treatment is the same.
class _WispColorChip extends StatelessWidget {
  const _WispColorChip({
    required this.color,
    required this.onEdit,
    required this.onRemove,
  });

  final LampColor color;
  final VoidCallback onEdit;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Stack(
      clipBehavior: Clip.none,
      children: [
        GestureDetector(
          onTap: onEdit,
          child: LampColorSwatch(color: color, size: 40),
        ),
        Positioned(
          top: -6,
          right: -6,
          child: Semantics(
            label: 'Remove color',
            button: true,
            child: InkWell(
              onTap: onRemove,
              customBorder: const CircleBorder(),
              child: Container(
                width: 18,
                height: 18,
                decoration: BoxDecoration(
                  color: colorScheme.surfaceContainerHighest,
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  Icons.close,
                  size: 12,
                  color: colorScheme.onSurface,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}
