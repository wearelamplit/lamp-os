import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../../../core/widgets/confirm_discard.dart';
import '../../domain/lamp_color.dart';
import 'color_picker_sheet.dart';

/// Reusable bottom sheet for editing a list of color stops: full-width color
/// bar + hex per row, add / remove / (optional) reorder, tap-a-row to edit its
/// color, live-preview via [onChanged] as the user edits, and Save / Cancel
/// with a discard guard. The sheet owns its working list; [onChanged] streams
/// every in-flight change for preview and [onSave] commits.
class ColorStopsSheet extends StatefulWidget {
  const ColorStopsSheet({
    super.key,
    required this.initial,
    required this.title,
    required this.max,
    required this.onSave,
    this.description,
    this.allowEmpty = false,
    this.reorderable = true,
    this.bpp = 4,
    this.onChanged,
  });

  final List<LampColor> initial;
  final String title;
  final int max;

  /// Muted help line under the title explaining how these colors are used.
  final String? description;

  /// Empty list allowed (min stays at zero); otherwise the last stop can't be
  /// removed.
  final bool allowEmpty;

  /// Drag-to-reorder rows. False renders a plain list with no drag handles for
  /// callers whose colors are an unordered set.
  final bool reorderable;

  /// Strip depth for the color picker: 4 exposes the warm-white slider, 3 hides
  /// it.
  final int bpp;

  /// Streams the working list on every edit for live preview. May be null.
  final ValueChanged<List<LampColor>>? onChanged;

  /// Commits the final list. Throws to signal a failed write (the sheet stays
  /// open and surfaces the error).
  final Future<void> Function(List<LampColor>) onSave;

  @override
  State<ColorStopsSheet> createState() => _ColorStopsSheetState();
}

class _ColorStopsSheetState extends State<ColorStopsSheet> {
  // Snapshot once. The parent re-watches lamp state and re-passes the
  // live-updated colors as `initial` on every rebuild, so the dirty check and
  // revert target must key off the colors the sheet opened with, not the prop.
  late final List<LampColor> _original = List.of(widget.initial);
  late List<LampColor> _colors = List.of(widget.initial);
  bool _allowPop = false;

  int get _minCount => widget.allowEmpty ? 0 : 1;

  bool get _dirty => !listEquals(_colors, _original);

  void _apply(List<LampColor> next) {
    setState(() => _colors = next);
    widget.onChanged?.call(List.of(next));
  }

  void _close({required bool revert}) {
    if (revert && _dirty) widget.onChanged?.call(List.of(_original));
    setState(() => _allowPop = true);
    // Pop after the frame so PopScope picks up _allowPop=true (setState +
    // synchronous Navigator.pop would still see the old canPop=false).
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) Navigator.of(context).pop();
    });
  }

  Future<void> _save() async {
    try {
      await widget.onSave(List.of(_colors));
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

  Future<void> _editStop(int i) async {
    final before = List.of(_colors);
    final picked = await showColorPickerSheet(
      context,
      initial: _colors[i],
      title: 'Stop ${i + 1}',
      bpp: widget.bpp,
      onLive: (live) {
        if (i >= _colors.length) return;
        _apply([..._colors]..[i] = live);
      },
    );
    if (picked == null) {
      _apply(before);
    } else if (i < _colors.length) {
      _apply([..._colors]..[i] = picked);
    }
  }

  void _removeStop(int i) {
    if (_colors.length <= _minCount) return;
    _apply([..._colors]..removeAt(i));
  }

  void _addStop() {
    if (_colors.length >= widget.max) return;
    // Seed from the last stop so the new one starts near a color the user can
    // tweak from, not a jarring white.
    final seed = _colors.isNotEmpty
        ? _colors.last
        : const LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0);
    _apply([..._colors, seed]);
  }

  void _reorder(int oldIndex, int newIndex) {
    final next = [..._colors];
    next.insert(newIndex, next.removeAt(oldIndex));
    _apply(next);
  }

  Widget _row(BuildContext ctx, int i) {
    final stop = _colors[i];
    final cs = Theme.of(ctx).colorScheme;
    final atMin = _colors.length <= _minCount;
    return ListTile(
      key: ValueKey('color-stop-$i'),
      onTap: () => _editStop(i),
      leading: widget.reorderable
          ? ReorderableDragStartListener(
              index: i,
              child: Icon(Icons.drag_indicator, color: cs.onSurfaceVariant),
            )
          : null,
      title: Row(
        children: [
          Expanded(
            child: Container(
              height: 36, // deliberate dimension, not spacing
              decoration: BoxDecoration(
                color: stop.toSwatch(),
                borderRadius: BorderRadius.circular(AppRadius.swatch),
                border: Border.all(
                  color: cs.outlineVariant,
                  width: 1, // deliberate dimension, not spacing
                ),
              ),
            ),
          ),
          const SizedBox(width: AppSpace.md),
          Text(
            '#${stop.toHex().substring(1, 7)}',
            style: Theme.of(ctx).textTheme.bodySmall?.copyWith(
                  color: cs.onSurfaceVariant,
                  fontFamily: 'monospace',
                ),
          ),
        ],
      ),
      trailing: Tooltip(
        message: atMin
            ? (widget.allowEmpty ? 'Remove color' : 'Keep at least one stop')
            : 'Remove stop',
        child: IconButton(
          icon: Icon(Icons.close, color: cs.onSurfaceVariant),
          onPressed: atMin ? null : () => _removeStop(i),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: _allowPop || !_dirty,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return;
        final discard = await confirmDiscard(context);
        if (discard) _close(revert: true);
      },
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(AppSpace.lg),
          child: Column(
            children: [
              Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  widget.title,
                  style: Theme.of(context).textTheme.titleMedium,
                ),
              ),
              if (widget.description != null) ...[
                const SizedBox(height: AppSpace.xs),
                Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                    widget.description!,
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: Theme.of(context).colorScheme.onSurfaceVariant,
                        ),
                  ),
                ),
              ],
              const SizedBox(height: AppSpace.md),
              Expanded(
                child: widget.reorderable
                    ? ReorderableListView.builder(
                        itemCount: _colors.length,
                        onReorderItem: _reorder,
                        buildDefaultDragHandles: false,
                        itemBuilder: _row,
                      )
                    : ListView.builder(
                        itemCount: _colors.length,
                        itemBuilder: _row,
                      ),
              ),
              if (_colors.length < widget.max) ...[
                const SizedBox(height: AppSpace.sm),
                SizedBox(
                  width: double.infinity,
                  child: FilledButton.tonalIcon(
                    onPressed: _addStop,
                    icon: const Icon(Icons.add, size: 18),
                    label: const Text('Add Color'),
                  ),
                ),
              ],
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

/// Opens [ColorStopsSheet] as a modal bottom sheet. Mirrors
/// [showColorPickerSheet].
Future<void> showColorStopsSheet(
  BuildContext context, {
  required List<LampColor> initial,
  required String title,
  required int max,
  required Future<void> Function(List<LampColor>) onSave,
  String? description,
  bool allowEmpty = false,
  bool reorderable = true,
  int bpp = 4,
  ValueChanged<List<LampColor>>? onChanged,
}) {
  return showAppSheet<void>(
    context,
    builder: (ctx) => FractionallySizedBox(
      heightFactor: 0.6,
      child: ColorStopsSheet(
        initial: initial,
        title: title,
        max: max,
        onSave: onSave,
        description: description,
        allowEmpty: allowEmpty,
        reorderable: reorderable,
        bpp: bpp,
        onChanged: onChanged,
      ),
    ),
  );
}
