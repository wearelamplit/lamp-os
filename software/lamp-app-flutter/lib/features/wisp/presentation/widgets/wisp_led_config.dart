import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/nav_row.dart';
import '../../../../core/widgets/number_input_dialog.dart';
import '../../../../core/widgets/section_header.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';

/// LED strip config panel. Pass [status] for initial values; [lampId]
/// routes writes through the wisp notifier.
class WispLedConfig extends ConsumerWidget {
  const WispLedConfig({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final textTheme = Theme.of(context).textTheme;
    final notifier = ref.read(wispNotifierProvider(lampId).notifier);
    final ledType = status.ledType;

    return Column(
      key: const Key('wisp-led-config'),
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SectionHeader('LED strip'),
        const SizedBox(height: AppSpace.xs),
        Text('LED type', style: textTheme.bodySmall),
        const SizedBox(height: AppSpace.sm),
        SegmentedButton<String>(
          key: const Key('wisp-led-type-picker'),
          style: SegmentedButton.styleFrom(
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
          ),
          segments: const [
            ButtonSegment(value: 'GRBW', label: Text('GRBW')),
            ButtonSegment(value: 'GRB', label: Text('GRB')),
            ButtonSegment(value: 'BGR', label: Text('BGR')),
          ],
          selected: {ledType},
          showSelectedIcon: false,
          onSelectionChanged: (set) {
            if (set.isEmpty) return;
            notifier.setLedStrip(set.first, status.pixelCount);
          },
        ),
        const SizedBox(height: AppSpace.sm),
        NavRow(
          key: const Key('wisp-led-count-row'),
          icon: Icons.lightbulb_outline,
          title: 'LED count',
          subtitle: '${status.pixelCount} LEDs',
          onTap: () => showNumberInputDialog(
            context,
            title: 'LED count',
            label: 'LED count',
            initial: status.pixelCount,
            min: 1,
            max: 100,
            onSave: (v) => notifier.setLedStrip(ledType, v),
          ),
        ),
      ],
    );
  }
}
