import 'package:collection/collection.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../domain/easing_curves.dart';
import '../../domain/expression_catalog.dart';

/// Tap-to-open control for the shared `easing` param. Shows
/// `Motion  <name>  ›` and opens a sheet with a scrollable grid of tiles,
/// each fully named by composing the catalog's `group` and `label`.
class MotionPicker extends StatelessWidget {
  const MotionPicker({
    super.key,
    required this.label,
    required this.options,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final List<EnumOption> options;
  final int value;
  final ValueChanged<int> onChanged;

  /// Composes `group`/`label` into one name: "Overshoot In", "Bounce
  /// In-out". Collapses to the group alone when the label adds nothing
  /// (Linear, Float, Random); falls back to the label when a legacy
  /// firmware catalog has no `group`.
  static String displayName(EnumOption o) {
    final group = o.group;
    if (group == null || group == o.label) return group ?? o.label;
    return '$group ${o.label}';
  }

  EnumOption get _selected =>
      options.firstWhereOrNull((o) => o.value == value) ?? options.first;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return InkWell(
      onTap: () => _open(context),
      borderRadius: BorderRadius.circular(AppRadius.card),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
        child: Row(
          children: [
            Text(label, style: textTheme.bodyMedium),
            const SizedBox(width: AppSpace.sm),
            Expanded(
              child: Text(
                displayName(_selected),
                textAlign: TextAlign.right,
                overflow: TextOverflow.ellipsis,
                style: textTheme.bodyMedium?.copyWith(
                  color: colorScheme.primary,
                ),
              ),
            ),
            const SizedBox(width: AppSpace.xs),
            Icon(Icons.chevron_right, color: colorScheme.onSurfaceVariant),
          ],
        ),
      ),
    );
  }

  Future<void> _open(BuildContext context) async {
    final picked = await showAppSheet<int>(
      context,
      builder: (ctx) => SafeArea(
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxHeight: MediaQuery.of(ctx).size.height * 0.7,
          ),
          child: _MotionSheet(label: label, options: options, value: value),
        ),
      ),
    );
    if (picked != null && picked != value) onChanged(picked);
  }
}

/// Sheet body: a Direction row (In / Out / In-out, hidden when the catalog
/// has no multi-direction family) above a Motion grid of family tiles.
/// Direction is in-sheet-only state so the grid's sparklines redraw live
/// before a tile confirms the pick.
class _MotionSheet extends StatefulWidget {
  const _MotionSheet({
    required this.label,
    required this.options,
    required this.value,
  });

  final String label;
  final List<EnumOption> options;
  final int value;

  @override
  State<_MotionSheet> createState() => _MotionSheetState();
}

class _MotionSheetState extends State<_MotionSheet> {
  late final List<EasingFamily> _families = groupEasing(widget.options);
  late final List<String> _directionLabels = _unionDirections(_families);
  late String _family;
  late String _direction;

  @override
  void initState() {
    super.initState();
    final current = describeEasing(widget.options, widget.value);
    _family = current.family;
    _direction = _directionLabels.contains(current.direction)
        ? current.direction
        : (_directionLabels.isEmpty
              ? current.direction
              : _directionLabels.first);
  }

  static List<String> _unionDirections(List<EasingFamily> families) {
    final labels = <String>[];
    for (final f in families) {
      if (f.directions.length <= 1) continue;
      for (final d in f.directions) {
        if (!labels.contains(d.label)) labels.add(d.label);
      }
    }
    return labels;
  }

  int _valueFor(EasingFamily family) =>
      family.directions.firstWhereOrNull((d) => d.label == _direction)?.value ??
      family.directions.first.value;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(
            AppSpace.lg,
            AppSpace.md,
            AppSpace.lg,
            AppSpace.sm,
          ),
          child: Text(widget.label, style: textTheme.titleMedium),
        ),
        if (_directionLabels.isNotEmpty) ...[
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: AppSpace.lg),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text('Direction', style: textTheme.bodyMedium),
                const SizedBox(height: AppSpace.xs),
                SegmentedButton<String>(
                  showSelectedIcon: false,
                  style: SegmentedButton.styleFrom(
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(AppRadius.card),
                    ),
                  ),
                  segments: [
                    for (final d in _directionLabels)
                      ButtonSegment(value: d, label: Text(d)),
                  ],
                  selected: {_direction},
                  onSelectionChanged: (s) =>
                      setState(() => _direction = s.first),
                ),
              ],
            ),
          ),
          const SizedBox(height: AppSpace.lg),
        ],
        Padding(
          padding: const EdgeInsets.fromLTRB(
            AppSpace.lg,
            0,
            AppSpace.lg,
            AppSpace.xs,
          ),
          child: Text('Feel', style: textTheme.bodyMedium),
        ),
        Flexible(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(
              AppSpace.lg,
              0,
              AppSpace.lg,
              AppSpace.lg,
            ),
            child: LayoutBuilder(
              builder: (context, constraints) {
                const columns = 4;
                final tileWidth =
                    (constraints.maxWidth - AppSpace.sm * (columns - 1)) /
                    columns;
                return Wrap(
                  spacing: AppSpace.sm,
                  runSpacing: AppSpace.sm,
                  children: [
                    for (final family in _families)
                      SizedBox(
                        width: tileWidth,
                        child: _MotionFamilyTile(
                          family: family,
                          value: _valueFor(family),
                          selected: family.family == _family,
                          onSelect: () =>
                              Navigator.pop(context, _valueFor(family)),
                        ),
                      ),
                  ],
                );
              },
            ),
          ),
        ),
      ],
    );
  }
}

class _MotionFamilyTile extends StatelessWidget {
  const _MotionFamilyTile({
    required this.family,
    required this.value,
    required this.selected,
    required this.onSelect,
  });

  final EasingFamily family;
  final int value;
  final bool selected;
  final VoidCallback onSelect;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onSelect,
        borderRadius: BorderRadius.circular(AppRadius.card),
        child: Container(
          padding: const EdgeInsets.all(AppSpace.sm),
          decoration: BoxDecoration(
            color: selected
                ? colorScheme.primary.withValues(alpha: 0.15)
                : Colors.transparent,
            border: Border.all(
              color: selected
                  ? colorScheme.primary
                  : colorScheme.outlineVariant,
              width: selected ? 2 : 1, // deliberate dimension, not spacing
            ),
            borderRadius: BorderRadius.circular(AppRadius.card),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              SizedBox(
                height: 28, // deliberate dimension, not spacing
                child: value == kRandomEasingValue
                    ? Icon(Icons.shuffle, color: colorScheme.primary)
                    : CustomPaint(
                        painter: EasingSparkline(
                          value: value,
                          line: colorScheme.primary,
                          axis: colorScheme.onSurfaceVariant,
                        ),
                      ),
              ),
              const SizedBox(height: AppSpace.sm),
              Text(
                family.family,
                textAlign: TextAlign.center,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Draws the easing curve [value] over [0,1] with y inverted (up = 1), plus a
/// faint baseline. Curve math mirrors the firmware via [applyEasing]. Never
/// construct with [kRandomEasingValue] — Random has no curve.
class EasingSparkline extends CustomPainter {
  EasingSparkline({
    required this.value,
    required this.line,
    required this.axis,
  });

  final int value;
  final Color line;
  final Color axis;

  @override
  void paint(Canvas canvas, Size size) {
    final axisPaint = Paint()
      ..color = axis.withValues(alpha: 0.3)
      ..strokeWidth = 1; // deliberate dimension, not spacing
    canvas.drawLine(
      Offset(0, size.height),
      Offset(size.width, size.height),
      axisPaint,
    );

    final linePaint = Paint()
      ..color = line
      ..strokeWidth =
          2 // deliberate dimension, not spacing
      ..style = PaintingStyle.stroke
      ..strokeJoin = StrokeJoin.round
      ..strokeCap = StrokeCap.round;

    const samples = 32;
    final path = Path();
    for (var i = 0; i <= samples; i++) {
      final t = i / samples;
      final dx = t * size.width;
      final dy = size.height - applyEasing(value, t) * size.height;
      if (i == 0) {
        path.moveTo(dx, dy);
      } else {
        path.lineTo(dx, dy);
      }
    }
    canvas.drawPath(path, linePaint);
  }

  @override
  bool shouldRepaint(EasingSparkline old) =>
      old.value != value || old.line != line || old.axis != axis;
}
