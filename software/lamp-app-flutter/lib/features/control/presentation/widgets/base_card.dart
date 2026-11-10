import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../application/control_notifier.dart';
import '../../domain/color_rotation.dart';
import '../../domain/lamp_color.dart';
import 'rotating_gradient.dart';
import 'rotation_card_parts.dart';

/// Slice of ControlState BaseCard needs: colors list + colorsEditable. Used as
/// the `.select` projection so sibling state changes don't trigger a rebuild.
class _BaseSlice {
  const _BaseSlice(
    this.colors, {
    required this.colorsEditable,
    required this.rotation,
  });
  final List<LampColor> colors;
  final bool colorsEditable;
  final ColorRotation? rotation;

  @override
  bool operator ==(Object other) {
    if (other is! _BaseSlice) return false;
    if (colorsEditable != other.colorsEditable) return false;
    if (rotation != other.rotation) return false;
    if (colors.length != other.colors.length) return false;
    for (var i = 0; i < colors.length; i++) {
      if (colors[i] != other.colors[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode =>
      Object.hash(Object.hashAll(colors), colorsEditable, rotation);
}

class BaseCard extends ConsumerWidget {
  const BaseCard({
    super.key,
    required this.lampId,
    required this.onTap,
    this.title = 'Base',
  });

  final String lampId;
  final VoidCallback onTap;
  final String title;

  /// Returns a gradient-safe list: LinearGradient requires ≥2 stops, so a
  /// single-color list is duplicated, and an empty list falls back to black.
  List<Color> _gradientColors(List<LampColor> colors) {
    if (colors.isEmpty) return const [Colors.black, Colors.black];
    final swatches = colors.map((c) => c.toSwatch()).toList();
    if (swatches.length == 1) return [swatches.first, swatches.first];
    return swatches;
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final slice = ref.watch(
      controlNotifierProvider(lampId).select((async) {
        final state = async.value;
        return _BaseSlice(
          state?.base.colors ?? const [],
          colorsEditable: state?.base.colorsEditable ?? true,
          rotation: state?.base.rotation,
        );
      }),
    );
    final cs = Theme.of(context).colorScheme;
    final rotating = effectiveRotation(slice.rotation, slice.colors);
    return InkWell(
      // Gate the tap: if the firmware marks base as non-editable, the card
      // is still visible (shows the current gradient) but tapping does nothing.
      onTap: slice.colorsEditable ? onTap : null,
      borderRadius: BorderRadius.circular(AppRadius.card),
      child: Container(
        margin: const EdgeInsets.symmetric(
            horizontal: AppSpace.lg, vertical: AppSpace.sm),
        padding: const EdgeInsets.all(AppSpace.lg),
        decoration: BoxDecoration(
          color: cs.surfaceContainer,
          borderRadius: BorderRadius.circular(AppRadius.card),
          border: Border.all(color: cs.outlineVariant),
        ),
        child: Row(
          children: [
            SizedBox(
              width: 56, // deliberate dimension, not spacing
              height: 56,
              child: rotating == null
                  ? Container(
                      decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(14),
                        gradient: LinearGradient(
                          begin: Alignment.topCenter,
                          end: Alignment.bottomCenter,
                          colors: _gradientColors(slice.colors),
                        ),
                      ),
                    )
                  : RotatingGradient(
                      colors: slice.colors,
                      rotation: rotating,
                      stop0AtBottom: false,
                      borderRadius: BorderRadius.circular(14),
                    ),
            ),
            const SizedBox(width: AppSpace.lg),
            Expanded(child: CardTitle(title: title, rotation: rotating)),
            if (slice.colorsEditable)
              Icon(Icons.chevron_right, color: cs.onSurfaceVariant),
          ],
        ),
      ),
    );
  }
}
