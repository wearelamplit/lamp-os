import 'package:collection/collection.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../domain/easing_curves.dart';
import '../../domain/expression_catalog.dart';

/// Tap-to-open control for the shared `easing` param. Shows `Motion  <name>  ›`
/// and opens a sheet listing each option with a curve sparkline, name, and a
/// one-liner. Selection order/values/labels come from the catalog descriptor.
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

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    final selected =
        options.firstWhereOrNull((o) => o.value == value) ?? options.first;
    return InkWell(
      onTap: () => _open(context),
      borderRadius: BorderRadius.circular(AppRadius.card),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
        child: Row(
          children: [
            Text(label, style: textTheme.bodyMedium),
            const Spacer(),
            Text(
              selected.label,
              style: textTheme.bodyMedium
                  ?.copyWith(color: colorScheme.primary),
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
      builder: (ctx) {
        final textTheme = Theme.of(ctx).textTheme;
        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(
                    AppSpace.lg, AppSpace.md, AppSpace.lg, AppSpace.sm),
                child: Text(label, style: textTheme.titleMedium),
              ),
              for (final o in options) _MotionOptionTile(
                option: o,
                selected: o.value == value,
                onTap: () => Navigator.pop(ctx, o.value),
              ),
              const SizedBox(height: AppSpace.sm),
            ],
          ),
        );
      },
    );
    if (picked != null && picked != value) onChanged(picked);
  }
}

class _MotionOptionTile extends StatelessWidget {
  const _MotionOptionTile({
    required this.option,
    required this.selected,
    required this.onTap,
  });

  final EnumOption option;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return ListTile(
      onTap: onTap,
      leading: SizedBox(
        width: 40, // deliberate dimension, not spacing
        height: 28,
        child: CustomPaint(
          painter: EasingSparkline(
            value: option.value,
            line: colorScheme.primary,
            axis: colorScheme.onSurfaceVariant,
          ),
        ),
      ),
      title: Text(option.label),
      subtitle: Text(easingBlurbs[option.label] ?? ''),
      trailing:
          selected ? Icon(Icons.check, color: colorScheme.primary) : null,
    );
  }
}

/// Draws the easing curve [value] over [0,1] with y inverted (up = 1), plus a
/// faint baseline. Curve math mirrors the firmware via [applyEasing].
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
    canvas.drawLine(Offset(0, size.height),
        Offset(size.width, size.height), axisPaint);

    final linePaint = Paint()
      ..color = line
      ..strokeWidth = 2 // deliberate dimension, not spacing
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
