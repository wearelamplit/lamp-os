import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/color_picker_sheet.dart';
import '../../../control/presentation/widgets/lamp_color_swatch.dart';
import '../../application/wisp_notifier.dart';

/// Off-mode swatch picker. In Off mode the wisp doesn't broadcast a
/// palette to the lamps (PaintDistributor is held off); the operator
/// can still pick a color for the wisp's own 30-pixel ring so it
/// "operates like a lamp" rather than sitting on the default candle-
/// amber. Tap the swatch to open the same color picker the manual
/// editor uses; the wisp persists the choice in NVS.
class OffColorPicker extends ConsumerStatefulWidget {
  const OffColorPicker({super.key, required this.lampId, required this.current});

  final String lampId;
  final LampColor current;

  @override
  ConsumerState<OffColorPicker> createState() => _OffColorPickerState();
}

class _OffColorPickerState extends ConsumerState<OffColorPicker> {
  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Wisp color'),
        const SizedBox(height: AppSpace.sm),
        Padding(
          padding: const EdgeInsets.only(bottom: AppSpace.md),
          child: Text(
            "Off doesn't broadcast a palette — your lamps stay on their "
            "own behaviour. Pick the color the wisp itself shows.",
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ),
        Row(
          children: [
            GestureDetector(
              onTap: _pick,
              child: LampColorSwatch(color: widget.current, size: 48),
            ),
            const SizedBox(width: AppSpace.md),
            Expanded(
              child: Text(
                widget.current.toHex().toUpperCase(),
                style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                  fontFamily: 'monospace',
                ),
              ),
            ),
            TextButton.icon(
              onPressed: _pick,
              icon: const Icon(Icons.edit, size: 16),
              label: const Text('Change'),
            ),
          ],
        ),
      ],
    );
  }

  Future<void> _pick() async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // onLive streams every drag tick into setOffColor; the notifier
    // debounces the BLE write so the wisp doesn't get flooded. Cancel
    // restores the original colour (one trailing write).
    final original = widget.current;
    final picked = await showColorPickerSheet(
      context,
      initial: original,
      title: 'Wisp color (Off)',
      bpp: 3,
      onLive: notifier.setOffColor,
    );
    if (picked == null) {
      await notifier.setOffColor(original);
    } else {
      await notifier.setOffColor(picked);
    }
  }
}
