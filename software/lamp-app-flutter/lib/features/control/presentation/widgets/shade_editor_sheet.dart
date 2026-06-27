import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../application/control_notifier.dart';
import '../../domain/lamp_color.dart';
import 'color_picker_sheet.dart';

/// Hard cap on shade gradient stops. Same rationale as `_kMaxBaseStops`
/// in base_editor_sheet.dart — firmware interp handles arbitrary N, the
/// cap is UX-only so the modal stays scannable.
const int _kMaxShadeStops = 6;

/// Modal sheet for editing the shade gradient stops. Mirrors
/// `BaseEditorSheet`'s session pattern — live-previews via the
/// controlNotifier as the user picks, snapshots the colors it opened
/// with, and either commits (Update just closes — state already
/// mirrors edits) or rolls back the live-preview channel (Cancel
/// re-writes the snapshot) before popping. Persistence still goes
/// through the AppBar's Save Changes (settings_blob).
///
/// Shade has no `ac` (active color index) concept — the shade gradient
/// has no per-pixel picker the way base does, so it's just stops with
/// even interpolation. Editor is simpler than `BaseEditorSheet` for the
/// same reason.
class ShadeEditorSheet extends ConsumerStatefulWidget {
  const ShadeEditorSheet({super.key, required this.lampId});

  final String lampId;

  @override
  ConsumerState<ShadeEditorSheet> createState() => _ShadeEditorSheetState();
}

class _ShadeEditorSheetState extends ConsumerState<ShadeEditorSheet> {
  /// Captured at first build when state is non-null. Cancel restores it.
  List<LampColor>? _originalColors;

  /// Set true ONLY when the user explicitly taps Save. Any other dismiss
  /// path (Cancel button, backdrop tap, system back) is treated as a
  /// revert.
  bool _committed = false;

  @override
  Widget build(BuildContext context) {
    final shade = ref.watch(
      controlNotifierProvider(widget.lampId)
          .select((async) => async.value?.shade),
    );
    if (shade == null) return const SizedBox.shrink();

    _originalColors ??= List.of(shade.colors);

    final colors = shade.colors;
    final notifier =
        ref.read(controlNotifierProvider(widget.lampId).notifier);

    Future<void> editStop(int i) async {
      // Tell the lamp we're operator-editing the shade surface so wisp
      // overrides get dropped for the picker's lifetime. Always pair
      // with a close call in `finally`.
      notifier.setEditSession(EditSurface.shade, true);
      try {
        final picked = await showColorPickerSheet(
          context,
          initial: colors[i],
          title: 'Stop ${i + 1}',
          bpp: shade.bpp,
          onLive: (live) {
            // Read fresh each tick so concurrent state changes don't
            // get clobbered.
            final current =
                ref.read(controlNotifierProvider(widget.lampId)).value;
            if (current == null) return;
            final next = [...current.shade.colors];
            if (i >= next.length) return; // stop removed while picker open
            next[i] = live;
            notifier.setShadeColors(next);
          },
        );
        if (picked == null) {
          // Cancelled — restore whatever the snapshot was at the moment
          // editStop opened. State already mirrors the live picks, so
          // we re-push the original list to roll back.
          final reverted = [...colors];
          notifier.setShadeColors(reverted);
        }
      } finally {
        notifier.setEditSession(EditSurface.shade, false);
      }
    }

    void removeStop(int i) {
      if (colors.length <= 1) return;
      final next = [...colors]..removeAt(i);
      notifier.setShadeColors(next);
    }

    void addStop() {
      if (colors.length >= _kMaxShadeStops) return;
      // Duplicate the last stop so the new one starts at a sensible color
      // the user can tweak from, rather than dropping to white and forcing
      // them to dial it back to something nearby.
      final seed = colors.isNotEmpty
          ? colors.last
          : const LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0);
      notifier.setShadeColors([...colors, seed]);
    }

    void reorder(int oldIndex, int newIndex) {
      final next = [...colors];
      final picked = next.removeAt(oldIndex);
      next.insert(newIndex, picked);
      notifier.setShadeColors(next);
    }

    void cancel() {
      // Roll back to the session baseline.
      final origColors = _originalColors;
      if (origColors != null) notifier.setShadeColors(origColors);
      Navigator.pop(context);
    }

    return PopScope(
      canPop: true,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop && !_committed && _originalColors != null) {
          notifier.setShadeColors(_originalColors!);
        }
      },
      child: SafeArea(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            const Row(
              children: [
                Text(
                  'Shade colors',
                  style: TextStyle(
                    color: BrandColors.lampWhite,
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Expanded(
              child: ReorderableListView.builder(
                itemCount: colors.length,
                onReorderItem: reorder,
                buildDefaultDragHandles: false,
                itemBuilder: (ctx, i) {
                  final stop = colors[i];
                  return ListTile(
                    key: ValueKey('shade-stop-$i'),
                    onTap: () => editStop(i),
                    leading: ReorderableDragStartListener(
                      index: i,
                      child: const Icon(Icons.drag_indicator,
                          color: BrandColors.slateGrey),
                    ),
                    title: Row(
                      children: [
                        Container(
                          width: 28,
                          height: 28,
                          decoration: BoxDecoration(
                            color: stop.toSwatch(),
                            shape: BoxShape.circle,
                            border: Border.all(
                              color: Colors.white.withValues(alpha: 0.12),
                              width: 1,
                            ),
                          ),
                        ),
                        const SizedBox(width: 12),
                        Text(
                          '#${stop.toHex().substring(1, 7)}',
                          style: const TextStyle(
                            color: BrandColors.fogGrey,
                            fontSize: 12,
                            fontFamily: 'monospace',
                          ),
                        ),
                      ],
                    ),
                    trailing: Tooltip(
                      message: colors.length <= 1
                          ? 'A gradient needs at least one stop'
                          : 'Remove stop',
                      child: IconButton(
                        icon: const Icon(Icons.close,
                            color: BrandColors.slateGrey),
                        onPressed: colors.length <= 1
                            ? null
                            : () => removeStop(i),
                      ),
                    ),
                  );
                },
              ),
            ),
            if (colors.length < _kMaxShadeStops)
              TextButton(
                onPressed: addStop,
                child: const Text('+ Add stop'),
              ),
            const SizedBox(height: 8),
            Row(
              children: [
                TextButton(
                  onPressed: cancel,
                  child: const Text('Cancel'),
                ),
                const Spacer(),
                FilledButton.icon(
                  icon: const Icon(Icons.check, size: 18),
                  label: const Text('Save'),
                  onPressed: () async {
                    final notifier = ref.read(
                      controlNotifierProvider(widget.lampId).notifier,
                    );
                    _committed = true;
                    try {
                      await notifier.commit();
                    } catch (e) {
                      if (!context.mounted) return;
                      ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(
                          content: Text("Couldn't save — disconnected"),
                        ),
                      );
                      return;
                    }
                    if (!context.mounted) return;
                    Navigator.of(context).pop();
                  },
                ),
              ],
            ),
          ],
        ),
      ),
    ),
    );
  }
}

Future<void> showShadeEditorSheet(
  BuildContext context, {
  required String lampId,
}) {
  return showAppSheet<void>(
    context,
    backgroundColor: BrandColors.midnightBlack,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
    ),
    builder: (ctx) => FractionallySizedBox(
      heightFactor: 0.6,
      child: ShadeEditorSheet(lampId: lampId),
    ),
  );
}
