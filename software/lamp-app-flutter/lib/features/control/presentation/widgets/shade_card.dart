import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../application/control_notifier.dart';
import '../../domain/color_rotation.dart';
import '../../domain/lamp_color.dart';
import 'rotating_gradient.dart';
import 'rotation_card_parts.dart';
import 'shade_editor_sheet.dart';

/// Slice of ControlState ShadeCard needs. Used as the `.select` projection so
/// sibling state changes don't trigger a rebuild.
class _ShadeSlice {
  const _ShadeSlice(
    this.colors, {
    required this.colorsEditable,
    required this.stop0AtBottom,
    required this.rotation,
  });
  final List<LampColor> colors;
  final bool colorsEditable;
  final ColorRotation? rotation;

  /// The standard lamp's shade has stop 0 at the bottom; every other variant's
  /// previews keep it at the top.
  final bool stop0AtBottom;

  @override
  bool operator ==(Object other) =>
      other is _ShadeSlice &&
      colorsEditable == other.colorsEditable &&
      stop0AtBottom == other.stop0AtBottom &&
      rotation == other.rotation &&
      const ListEquality<LampColor>().equals(colors, other.colors);

  @override
  int get hashCode =>
      Object.hash(
          Object.hashAll(colors), colorsEditable, stop0AtBottom, rotation);
}

class ShadeCard extends ConsumerWidget {
  const ShadeCard({
    super.key,
    required this.lampId,
    this.title = 'Shade',
    required this.spec,
    this.onEditSessionChanged,
  });

  final String lampId;
  final String title;

  /// Which color list this card previews and edits.
  final ColorChannelSpec spec;

  /// Fires `true` when the editor opens and `false` when it closes. Spans the
  /// whole sheet; ControlScreen drives the shade wisp edit-session from it so
  /// overrides drop for the entire edit, not just each picker drag.
  final ValueChanged<bool>? onEditSessionChanged;

  /// Returns a gradient-safe list: LinearGradient requires ≥2 stops, so a
  /// single-color list is duplicated, and an empty list falls back to black.
  /// (Matches `BaseCard._gradientColors`.)
  List<Color> _gradientColors(List<LampColor> colors) {
    if (colors.isEmpty) return const [Colors.black, Colors.black];
    final swatches = colors.map((c) => c.toSwatch()).toList();
    if (swatches.length == 1) return [swatches.first, swatches.first];
    return swatches;
  }

  Future<void> _onTap(BuildContext context) async {
    onEditSessionChanged?.call(true);
    try {
      await showShadeEditorSheet(context, lampId: lampId, spec: spec);
    } finally {
      onEditSessionChanged?.call(false);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final slice = ref.watch(
      controlNotifierProvider(lampId).select((async) {
        final state = async.value;
        return _ShadeSlice(
          state == null ? const [] : spec.selectColors(state),
          colorsEditable: state == null || state.shade.colorsEditable,
          stop0AtBottom: state?.lamp.isStandard ?? true,
          rotation: state == null ? null : spec.selectRotation(state),
        );
      }),
    );
    final cs = Theme.of(context).colorScheme;
    final rotating = effectiveRotation(slice.rotation, slice.colors);
    return InkWell(
      // Gate the tap: if the firmware marks shade as non-editable, the card
      // is still visible (shows the current gradient) but tapping does nothing.
      onTap: slice.colorsEditable ? () => _onTap(context) : null,
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
                          begin: slice.stop0AtBottom
                              ? Alignment.bottomCenter
                              : Alignment.topCenter,
                          end: slice.stop0AtBottom
                              ? Alignment.topCenter
                              : Alignment.bottomCenter,
                          colors: _gradientColors(slice.colors),
                        ),
                      ),
                    )
                  : RotatingGradient(
                      colors: slice.colors,
                      rotation: rotating,
                      stop0AtBottom: slice.stop0AtBottom,
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
