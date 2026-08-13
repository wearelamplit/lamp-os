import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';

/// Top space-dimmer control above the palette gradient bar. Scales every
/// claimed lamp together without touching their colors. Deliberately relative
/// (no absolute %) so the number never reads as a specific lamp brightness.
class SpaceBrightnessSlider extends ConsumerStatefulWidget {
  const SpaceBrightnessSlider({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<SpaceBrightnessSlider> createState() =>
      _SpaceBrightnessSliderState();
}

class _SpaceBrightnessSliderState extends ConsumerState<SpaceBrightnessSlider> {
  // Local slider state is authoritative after mount; the wisp echoes back
  // the written value, so re-seeding on every status change would fight a drag.
  late double _value;

  @override
  void initState() {
    super.initState();
    _value = widget.status.brightness.clamp(0, 100).toDouble();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    return Material(
      color: theme.colorScheme.surface,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(
            AppSpace.lg, AppSpace.sm, AppSpace.lg, AppSpace.sm),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.brightness_6_outlined,
                    size: 18, color: theme.colorScheme.secondary),
                const SizedBox(width: AppSpace.sm),
                Text('Relative Brightness',
                    style: theme.textTheme.titleSmall),
              ],
            ),
            Text(
              'Dims all lamps in this space together.',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
            Slider(
              min: 0,
              max: 100,
              value: _value,
              label: _value >= 99.5 ? 'Full' : 'Dimmer',
              onChanged: (v) {
                setState(() => _value = v);
                notifier.setBrightness(v.round());
              },
              onChangeEnd: (_) => notifier.flushBrightness(),
            ),
          ],
        ),
      ),
    );
  }
}
