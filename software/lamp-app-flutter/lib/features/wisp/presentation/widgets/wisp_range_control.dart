import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';

/// Step names matching the firmware's claim-range steps 0..3.
const List<String> kRangeStepNames = ['Close', 'Camp', 'Stage', 'Wide'];

/// Rough reach per step, crowd..empty-field, from the RSSI floor and a
/// 2.4 GHz path-loss midpoint. Bodies soak signal, so the low end is a
/// packed space and the high end open air.
const List<String> kRangeStepMeters = [
  '≈ 4–6 m',
  '≈ 7–16 m',
  '≈ 11–30 m',
  '≈ 19–60 m',
];

/// Claim-range slider. A deliberate slider (not a segmented picker): reach
/// is a continuum in the operator's head even though the wire carries four
/// steps. Pass the wisp's current [status] for the initial position and
/// [lampId] to route writes.
class WispRangeControl extends ConsumerStatefulWidget {
  const WispRangeControl({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<WispRangeControl> createState() => _WispRangeControlState();
}

class _WispRangeControlState extends ConsumerState<WispRangeControl> {
  // Local slider state is authoritative after mount; the wisp echoes back
  // the written value, so a re-seed on every status change would fight an
  // in-progress drag.
  late int _step;

  @override
  void initState() {
    super.initState();
    _step = widget.status.rangeStep.clamp(0, kRangeStepNames.length - 1);
  }

  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    final muted = Theme.of(context).textTheme.bodySmall?.copyWith(
          color: Theme.of(context).colorScheme.onSurfaceVariant,
        );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Range'),
        const SizedBox(height: AppSpace.xs),
        _LabelRow(left: 'Reach', right: kRangeStepNames[_step]),
        Slider(
          key: const Key('wisp-range-slider'),
          min: 0,
          max: (kRangeStepNames.length - 1).toDouble(),
          divisions: kRangeStepNames.length - 1,
          value: _step.toDouble(),
          onChanged: (v) => setState(() => _step = v.round()),
          onChangeEnd: (v) => notifier.setRange(v.round()),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('Close', style: muted),
              Text('Wide', style: muted),
            ],
          ),
        ),
        const SizedBox(height: AppSpace.xs),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
          child: Text(
            '${kRangeStepMeters[_step]} — shrinks as the space fills',
            key: const Key('wisp-range-info'),
            style: muted,
          ),
        ),
      ],
    );
  }
}

class _LabelRow extends StatelessWidget {
  const _LabelRow({required this.left, required this.right});

  final String left;
  final String right;

  @override
  Widget build(BuildContext context) {
    final muted = Theme.of(context).textTheme.bodySmall?.copyWith(
          color: Theme.of(context).colorScheme.onSurfaceVariant,
        );
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(left, style: muted),
          Text(right, style: muted),
        ],
      ),
    );
  }
}
