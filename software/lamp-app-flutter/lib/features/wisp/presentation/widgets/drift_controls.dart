import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';

/// Maps a slider position [0..1] to milliseconds [30000..3600000]
/// on a logarithmic scale (base 120). p=0 → 30 s, p=1 → 60 min.
int posToMs(double p) =>
    (30000 * pow(120.0, p)).round().clamp(30000, 3600000);

/// Inverse of [posToMs]: milliseconds → slider position [0..1].
double msToPos(int ms) => log(ms / 30000.0) / log(120.0);

/// Two-slider drift panel. Pass the wisp's current [status] for initial
/// slider positions and [lampId] to route writes.
class DriftControls extends ConsumerStatefulWidget {
  const DriftControls({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<DriftControls> createState() => _DriftControlsState();
}

class _DriftControlsState extends ConsumerState<DriftControls> {
  // Local slider state is authoritative after mount; the wisp echoes back
  // the written value, so a re-seed on every status change would fight an
  // in-progress drag.
  late double _pos;
  late int _fadePct;

  @override
  void initState() {
    super.initState();
    _pos = msToPos(widget.status.driftIntervalMs).clamp(0.0, 1.0);
    _fadePct = widget.status.driftFadePct.clamp(0, 100);
  }

  String _intervalLabel() {
    final ms = posToMs(_pos);
    if (ms < 60000) return '${(ms / 1000).round()}s';
    if (ms < 3600000) return '${(ms / 60000).round()}m';
    return '1h';
  }

  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Color drift'),
        const SizedBox(height: AppSpace.xs),
        _LabelRow(left: 'How often', right: _intervalLabel()),
        Slider(
          min: 0,
          max: 1,
          value: _pos,
          onChanged: (v) {
            setState(() => _pos = v);
            notifier.setDrift(posToMs(v), _fadePct);
          },
          onChangeEnd: (_) => notifier.flushDrift(),
        ),
        const SizedBox(height: AppSpace.xs),
        _LabelRow(left: 'Fade', right: '$_fadePct%'),
        Slider(
          min: 0,
          max: 100,
          divisions: 100,
          value: _fadePct.toDouble(),
          onChanged: (v) {
            setState(() => _fadePct = v.round());
            notifier.setDrift(posToMs(_pos), v.round());
          },
          onChangeEnd: (_) => notifier.flushDrift(),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'Snappy',
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
              ),
              Text(
                'Flowing',
                style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      color: Theme.of(context).colorScheme.onSurfaceVariant,
                    ),
              ),
            ],
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
