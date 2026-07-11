import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../application/control_notifier.dart';
import '../../domain/lamp_color.dart';
import 'shade_editor_sheet.dart';

/// Slice of ControlState ShadeCard needs: colors list + colorsEditable. Used
/// as the `.select` projection so sibling state changes don't rebuild us.
/// (Mirrors `_BaseSlice` in base_card.dart now that shade is a gradient too.)
class _ShadeSlice {
  const _ShadeSlice(this.colors, {required this.colorsEditable});
  final List<LampColor> colors;
  final bool colorsEditable;

  @override
  bool operator ==(Object other) =>
      other is _ShadeSlice &&
      colorsEditable == other.colorsEditable &&
      const ListEquality<LampColor>().equals(colors, other.colors);

  @override
  int get hashCode => Object.hash(Object.hashAll(colors), colorsEditable);
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
  /// (Mirrors `BaseCard._gradientColors`.)
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
        );
      }),
    );
    final cs = Theme.of(context).colorScheme;
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
            Container(
              width: 56, // deliberate dimension, not spacing
              height: 56,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(14),
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: _gradientColors(slice.colors),
                ),
              ),
            ),
            const SizedBox(width: AppSpace.lg),
            Expanded(
              child: Text(
                title,
                style: Theme.of(context).textTheme.titleMedium,
              ),
            ),
            if (slice.colorsEditable)
              Icon(Icons.chevron_right, color: cs.onSurfaceVariant),
          ],
        ),
      ),
    );
  }
}
