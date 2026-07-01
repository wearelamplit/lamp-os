import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../../../core/widgets/confirm_discard.dart';
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
/// with, and either commits (Save writes BLE + closes) or reverts the
/// live-preview channel (Cancel/discard re-writes the snapshot). A
/// discard-guard dialog blocks accidental dismissal when edits are pending.
class ShadeEditorSheet extends ConsumerStatefulWidget {
  const ShadeEditorSheet({super.key, required this.lampId});

  final String lampId;

  @override
  ConsumerState<ShadeEditorSheet> createState() => _ShadeEditorSheetState();
}

class _ShadeEditorSheetState extends ConsumerState<ShadeEditorSheet> {
  List<LampColor>? _originalColors;

  /// True once we've signalled PopScope to allow the pop. Set just before
  /// the addPostFrameCallback-deferred Navigator.pop so the route doesn't
  /// get blocked by canPop=false on the same frame.
  bool _allowPop = false;

  bool _hasUnsavedChanges(List<LampColor> colors) =>
      _originalColors != null && !listEquals(colors, _originalColors);

  void _close({required bool revert}) {
    if (revert && _originalColors != null) {
      ref
          .read(controlNotifierProvider(widget.lampId).notifier)
          .setShadeColors(_originalColors!);
    }
    setState(() => _allowPop = true);
    // Pop after the frame so PopScope picks up _allowPop=true (setState +
    // synchronous Navigator.pop would still see the old canPop=false).
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) Navigator.of(context).pop();
    });
  }

  Future<void> _save() async {
    final notifier =
        ref.read(controlNotifierProvider(widget.lampId).notifier);
    try {
      await notifier.commit();
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Couldn't save: disconnected")),
      );
      return;
    }
    if (!mounted) return;
    _close(revert: false);
  }

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

    return PopScope(
      canPop: _allowPop || !_hasUnsavedChanges(colors),
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return; // no unsaved changes — already popped
        final discard = await confirmDiscard(context);
        if (discard) _close(revert: true);
      },
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(AppSpace.lg),
          child: Column(
            children: [
              Row(
                children: [
                  Text(
                    'Shade colors',
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ],
              ),
              const SizedBox(height: AppSpace.md),
              Expanded(
                child: ReorderableListView.builder(
                  itemCount: colors.length,
                  onReorderItem: reorder,
                  buildDefaultDragHandles: false,
                  itemBuilder: (ctx, i) {
                    final stop = colors[i];
                    final cs = Theme.of(ctx).colorScheme;
                    return ListTile(
                      key: ValueKey('shade-stop-$i'),
                      onTap: () => editStop(i),
                      leading: ReorderableDragStartListener(
                        index: i,
                        child: Icon(Icons.drag_indicator,
                            color: cs.onSurfaceVariant),
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
                                color: cs.outlineVariant,
                                width: 1,
                              ),
                            ),
                          ),
                          const SizedBox(width: AppSpace.md),
                          Text(
                            '#${stop.toHex().substring(1, 7)}',
                            style:
                                Theme.of(ctx).textTheme.bodySmall?.copyWith(
                                      color: cs.onSurfaceVariant,
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
                          icon: Icon(Icons.close, color: cs.onSurfaceVariant),
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
              const SizedBox(height: AppSpace.sm),
              Row(
                children: [
                  TextButton(
                    onPressed: () => _close(revert: true),
                    child: const Text('Cancel'),
                  ),
                  const Spacer(),
                  FilledButton.icon(
                    icon: const Icon(Icons.check, size: 18),
                    label: const Text('Save'),
                    onPressed: _save,
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
    builder: (ctx) => FractionallySizedBox(
      heightFactor: 0.6,
      child: ShadeEditorSheet(lampId: lampId),
    ),
  );
}
