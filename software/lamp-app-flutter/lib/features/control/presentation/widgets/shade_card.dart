import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
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
  bool operator ==(Object other) {
    if (other is! _ShadeSlice) return false;
    if (colorsEditable != other.colorsEditable) return false;
    if (colors.length != other.colors.length) return false;
    for (var i = 0; i < colors.length; i++) {
      if (colors[i] != other.colors[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode => Object.hash(Object.hashAll(colors), colorsEditable);
}

class ShadeCard extends ConsumerWidget {
  const ShadeCard({
    super.key,
    required this.lampId,
    this.onEditSessionChanged,
  });

  final String lampId;

  /// Fires `true` when the editor opens and `false` when it closes. The
  /// editor still wraps each per-stop pick in its own setEditSession
  /// scope (so wisp overrides drop only during the actual color picker
  /// drag); this callback is the outer sheet-open/close signal that
  /// callers (ControlScreen) use to drive their own UI affordances.
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
      await showShadeEditorSheet(context, lampId: lampId);
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
          state?.shade.colors ?? const [],
          colorsEditable: state?.shade.colorsEditable ?? true,
        );
      }),
    );
    return InkWell(
      // Gate the tap: if the firmware marks shade as non-editable, the card
      // is still visible (shows the current gradient) but tapping does nothing.
      onTap: slice.colorsEditable ? () => _onTap(context) : null,
      borderRadius: BorderRadius.circular(12),
      child: Container(
        margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: Colors.white.withValues(alpha: 0.04),
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
        ),
        child: Row(
          children: [
            Container(
              width: 56,
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
            const SizedBox(width: 16),
            const Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Shade',
                    style: TextStyle(
                      color: BrandColors.lampWhite,
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                      letterSpacing: 0.4,
                    ),
                  ),
                ],
              ),
            ),
            if (slice.colorsEditable)
              const Icon(Icons.chevron_right, color: BrandColors.slateGrey),
          ],
        ),
      ),
    );
  }
}
