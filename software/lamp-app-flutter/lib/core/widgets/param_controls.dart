import 'package:flutter/material.dart';

import '../theme/app_spacing.dart';

/// The label line above a parameter control.
Widget controlLabel(BuildContext context, String text) => Padding(
      padding: const EdgeInsets.only(bottom: AppSpace.xs),
      child: Text(text, style: Theme.of(context).textTheme.bodyMedium),
    );

/// The muted help line under a parameter control's label.
Widget paramHelpText(BuildContext context, String text) => Text(
      text,
      style: TextStyle(
        color: Theme.of(context).colorScheme.onSurfaceVariant,
        fontSize: 11,
      ),
    );

/// Snap [raw] to the nearest [step] within [min]..[max].
int snapToStep(int raw, int min, int max, int step) {
  if (step <= 1) return raw.clamp(min, max);
  final snapped = min + ((raw - min) / step).round() * step;
  return snapped.clamp(min, max);
}

/// Slider divisions for an integer range walked in [step]s.
int sliderDivisions(int min, int max, int step) {
  if (max <= min) return 1;
  final d = ((max - min) / (step <= 0 ? 1 : step)).round();
  return d < 1 ? 1 : d;
}

/// A labelled one-of-N choice drawn as a segmented button.
class ParamSegmented extends StatelessWidget {
  const ParamSegmented({
    super.key,
    required this.label,
    required this.options,
    required this.selectedIndex,
    required this.onIndex,
    this.help,
  });

  final String label;
  final List<String> options;
  final int selectedIndex;
  final ValueChanged<int> onIndex;
  final String? help;

  @override
  Widget build(BuildContext context) => Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          controlLabel(context, label),
          if (help != null)
            Padding(
              padding: const EdgeInsets.only(bottom: AppSpace.xs),
              child: paramHelpText(context, help!),
            ),
          SegmentedButton<int>(
            showSelectedIcon: false,
            style: SegmentedButton.styleFrom(
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(AppRadius.card),
              ),
            ),
            segments: [
              for (var i = 0; i < options.length; i++)
                ButtonSegment(value: i, label: Text(options[i])),
            ],
            selected: {selectedIndex},
            onSelectionChanged: (s) => onIndex(s.first),
          ),
        ],
      );
}

/// A labelled integer slider: label, optional help, optional end labels
/// either side of the track, and the formatted value right-aligned below.
class ParamSlider extends StatelessWidget {
  const ParamSlider({
    super.key,
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    required this.format,
    this.step = 1,
    this.leftLabel,
    this.rightLabel,
    this.help,
    this.invert = false,
  });

  final String label;
  final int value;
  final int min;
  final int max;
  final int step;

  /// Null renders the disabled slider state (a parent toggle gates it off).
  final ValueChanged<int>? onChanged;
  final String Function(int) format;
  final String? leftLabel;
  final String? rightLabel;
  final String? help;

  /// Mirrors the thumb high↔low while storage stays in original units. Used
  /// where "right = faster" conflicts with the stored value's direction.
  final bool invert;

  @override
  Widget build(BuildContext context) {
    final clamped = value.clamp(min, max);
    final sliderValue = (invert ? (min + max) - clamped : clamped).toDouble();
    final hasEnds = leftLabel != null && rightLabel != null;
    final cb = onChanged;
    final slider = Slider(
      value: sliderValue.clamp(min.toDouble(), max.toDouble()),
      min: min.toDouble(),
      max: max.toDouble(),
      divisions: sliderDivisions(min, max, step),
      onChanged: cb == null
          ? null
          : (v) {
              final raw = snapToStep(v.round(), min, max, step);
              cb(invert ? (min + max) - raw : raw);
            },
    );
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        controlLabel(context, label),
        if (help != null) paramHelpText(context, help!),
        Row(
          children: [
            if (hasEnds)
              Text(leftLabel!, style: TextStyle(color: muted, fontSize: 11)),
            Expanded(child: slider),
            if (hasEnds)
              Text(rightLabel!, style: TextStyle(color: muted, fontSize: 11)),
          ],
        ),
        Align(
          alignment: Alignment.centerRight,
          child: Padding(
            padding: const EdgeInsets.only(right: AppSpace.xs),
            child: Text(
              format(clamped),
              style: TextStyle(
                color: muted,
                fontSize: 12,
                fontFamily: 'monospace',
              ),
            ),
          ),
        ),
      ],
    );
  }
}
