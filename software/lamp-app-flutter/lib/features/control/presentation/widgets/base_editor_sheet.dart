import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../application/control_notifier.dart';
import '../../domain/lamp_color.dart';
import 'color_picker_sheet.dart';

/// Hard cap on base gradient stops. Bumped from 5 → 6 (commit-equivalent
/// of "give the user one more notch of expressive room"). Firmware
/// interp handles arbitrary N via even-spacing so no firmware change
/// is needed for this bump; the only reason to keep a finite cap is
/// UX (a row list with 6 stops still fits the modal comfortably; 10+
/// would crowd the picker).
const int _kMaxBaseStops = 6;

/// Modal sheet for editing the base gradient stops. Acts as an atomic edit
/// session: every in-sheet change live-previews via the controlNotifier
/// (so the lamp tracks the gradient as the user picks), but the sheet
/// keeps a snapshot of the colors/ac it opened with and either commits
/// (Save just closes — state already mirrors edits) or rolls back the
/// live-preview channel (Cancel re-writes the snapshot) before popping.
/// The user's "what's actually persisted" still comes from global
/// Save Changes via settings_blob.
class BaseEditorSheet extends ConsumerStatefulWidget {
  const BaseEditorSheet({super.key, required this.lampId});

  final String lampId;

  @override
  ConsumerState<BaseEditorSheet> createState() => _BaseEditorSheetState();
}

class _BaseEditorSheetState extends ConsumerState<BaseEditorSheet> {
  /// Captured at first build when state is non-null. Cancel restores both.
  List<LampColor>? _originalColors;
  int? _originalAc;

  /// Set true ONLY when the user explicitly taps Save. Any other dismiss
  /// path (Cancel button, backdrop tap, system back) is treated as a
  /// revert.
  bool _committed = false;

  @override
  Widget build(BuildContext context) {
    // Slice to just the base section. Without `.select` the sheet
    // rebuilds on every ControlState change (brightness, shade,
    // expressions, etc) — and a color-picker drag spams notifier
    // updates, all of which would re-render the whole sheet. With
    // the slice, only mutations to `state.base` trigger a rebuild.
    final base = ref.watch(
      controlNotifierProvider(widget.lampId)
          .select((async) => async.value?.base),
    );
    if (base == null) return const SizedBox.shrink();

    // First successful paint snapshots the session baseline.
    _originalColors ??= List.of(base.colors);
    _originalAc ??= base.ac;

    final colors = base.colors;
    final activeIndex = base.ac;
    final notifier =
        ref.read(controlNotifierProvider(widget.lampId).notifier);

    Future<void> editStop(int i) async {
      final original = [...colors];
      // Tell the lamp we're operator-editing the base surface so the
      // wisp's overrides get dropped for the picker's lifetime. Always
      // pair with a close call in `finally`.
      notifier.setEditSession(EditSurface.base, true);
      try {
        final picked = await showColorPickerSheet(
          context,
          initial: colors[i],
          title: 'Stop ${i + 1}',
          bpp: base.bpp,
          onLive: (live) {
            // Latest colors come from the notifier — read fresh each tick so
            // concurrent state changes (e.g. another stop edited in parallel)
            // don't get clobbered. In practice the picker is modal so this is
            // belt-and-suspenders, but it matches the realtime contract.
            final current =
                ref.read(controlNotifierProvider(widget.lampId)).value;
            if (current == null) return;
            final next = [...current.base.colors];
            if (i >= next.length) return; // stop removed while picker open
            next[i] = live;
            notifier.setBaseColors(next);
          },
        );
        if (picked == null) {
          // Cancelled — restore the snapshot we took before the picker opened.
          unawaited(notifier.setBaseColors(original));
        }
        // Save case: the last onLive tick already wrote the correct color;
        // no further action needed.
      } finally {
        notifier.setEditSession(EditSurface.base, false);
      }
    }

    void removeStop(int i) {
      if (colors.length <= 1) return;
      final next = [...colors]..removeAt(i);
      notifier.setBaseColors(next);
      if (activeIndex >= next.length) notifier.setBaseAc(next.length - 1);
    }

    void addStop() {
      if (colors.length >= _kMaxBaseStops) return;
      // Duplicate the last stop so the new one starts at a sensible color
      // the user can tweak from, rather than dropping to white and forcing
      // them to dial it back to something nearby.
      final seed = colors.isNotEmpty
          ? colors.last
          : const LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0);
      notifier.setBaseColors([...colors, seed]);
    }

    void reorder(int oldIndex, int newIndex) {
      final next = [...colors];
      final picked = next.removeAt(oldIndex);
      next.insert(newIndex, picked);
      notifier.setBaseColors(next);
      if (activeIndex == oldIndex) {
        notifier.setBaseAc(newIndex);
      }
    }

    void cancel() {
      // Roll back to the session baseline: re-write the live-preview channel
      // so the lamp visually reverts, and restore the active-stop index.
      // Subtle: setBaseColors mutates app state too, so this also clears any
      // edits the user made in this session from the global draft.
      final origColors = _originalColors;
      final origAc = _originalAc;
      if (origColors != null) notifier.setBaseColors(origColors);
      if (origAc != null) notifier.setBaseAc(origAc);
      Navigator.pop(context);
    }

    return PopScope(
      canPop: true,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop && !_committed) {
          if (_originalColors != null) {
            notifier.setBaseColors(_originalColors!);
          }
          if (_originalAc != null) notifier.setBaseAc(_originalAc!);
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
                  'Base colors',
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
                    key: ValueKey('stop-$i'),
                    onTap: () {
                      notifier.setBaseAc(i);
                      editStop(i);
                    },
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
                              color: i == activeIndex
                                  ? BrandColors.glowPink
                                  : Colors.white.withValues(alpha: 0.12),
                              width: i == activeIndex ? 2 : 1,
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
            if (colors.length < _kMaxBaseStops)
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
                      final cur = ref
                          .read(controlNotifierProvider(widget.lampId))
                          .value;
                      if (cur != null &&
                          _originalAc != null &&
                          cur.base.ac != _originalAc) {
                        await notifier.writeSettingsBlob(
                          {'base': {'ac': cur.base.ac}},
                          reboot: false,
                        );
                      }
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

Future<void> showBaseEditorSheet(
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
      child: BaseEditorSheet(lampId: lampId),
    ),
  );
}
