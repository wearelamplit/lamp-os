import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../application/control_notifier.dart';

/// Brightness slider card. Watches its own slice of the control state via
/// `.select(state.lamp.brightness)` so unrelated state mutations (colors,
/// expressions, home, etc.) don't trigger card rebuilds. Material's
/// Slider tracks the gesture position internally during a drag, so a
/// vanilla controlled value reads as 1:1 responsive without needing
/// local state.
class BrightnessCard extends ConsumerWidget {
  const BrightnessCard({
    super.key,
    required this.lampId,
    this.onEditSessionChanged,
  });

  final String lampId;

  /// Fires `true` when the slider drag starts and `false` when it ends.
  /// Wired into `ControlNotifier.setEditSession` so the lamp drops
  /// wisp-sourced brightness overrides for the duration of the drag.
  final ValueChanged<bool>? onEditSessionChanged;

  /// Interpolates the slider thumb black → amber-gold → white as brightness
  /// goes 0 → 50 → 100 %. Ported from the prior Vue app's BrightnessSlider
  /// (`software/lamp-app/src/components/BrightnessSlider.vue:27-48`). The
  /// amber midpoint visually signals "warm light, partially dimmed."
  static Color _thumbColorFor(int pct) {
    if (pct <= 50) {
      return Color.lerp(
          BrandColors.midnightBlack, BrandColors.amberGold, pct / 50)!;
    }
    return Color.lerp(
        BrandColors.amberGold, BrandColors.lampWhite, (pct - 50) / 50)!;
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final value = ref.watch(
      controlNotifierProvider(lampId)
          .select((async) => async.value?.lamp.brightness ?? 0),
    );
    final notifier = ref.read(controlNotifierProvider(lampId).notifier);
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Text(
                'Brightness',
                style: TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  letterSpacing: 0.4,
                ),
              ),
              const Spacer(),
              Text(
                '$value%',
                style: const TextStyle(
                  color: BrandColors.fogGrey,
                  fontSize: 12,
                ),
              ),
            ],
          ),
          SliderTheme(
            data: SliderTheme.of(context).copyWith(
              thumbColor: _thumbColorFor(value),
              // Track stays on the brand palette; only the thumb morphs so
              // the cue is unmistakable but the chrome stays consistent.
              activeTrackColor: BrandColors.amberGold.withValues(alpha: 0.5),
            ),
            child: Slider(
              min: 0,
              max: 100,
              divisions: 100,
              value: value.toDouble(),
              onChanged: (v) => notifier.setBrightness(v.toInt()),
              onChangeStart: (_) => onEditSessionChanged?.call(true),
              onChangeEnd: (v) {
                notifier.setBrightness(v.toInt()); // fence: land the final value
                notifier.scheduleBrightnessCommit();
                onEditSessionChanged?.call(false);
              },
            ),
          ),
        ],
      ),
    );
  }
}
