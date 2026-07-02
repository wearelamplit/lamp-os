import 'package:flutter/material.dart';

/// A themed dual-grab range slider for interval bounds.
///
/// Wraps Material [RangeSlider] with optional thumb labels via [labelFor].
/// Colors inherit from [SliderTheme] / the ambient [ColorScheme] — no
/// hardcoded colors.
class IntervalRangeSlider extends StatelessWidget {
  const IntervalRangeSlider({
    required this.values,
    required this.min,
    required this.max,
    required this.onChanged,
    this.labelFor,
    this.leftLabel,
    this.rightLabel,
    super.key,
  });

  final RangeValues values;
  final double min;
  final double max;
  final ValueChanged<RangeValues> onChanged;

  /// Optional formatter applied to each thumb value for the popup label.
  /// Receives the raw slider value; returns the display string.
  final String Function(double)? labelFor;

  /// Optional endpoint hints flanking the track (e.g. 'often' / 'rare'). When
  /// both are set the slider also shows a right-aligned current-range caption
  /// (via [labelFor]), matching the param sliders' look.
  final String? leftLabel;
  final String? rightLabel;

  @override
  Widget build(BuildContext context) {
    final slider = RangeSlider(
      values: values,
      min: min,
      max: max,
      onChanged: onChanged,
      labels: labelFor == null
          ? null
          : RangeLabels(labelFor!(values.start), labelFor!(values.end)),
    );
    if (leftLabel == null || rightLabel == null) return slider;
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    final endStyle = TextStyle(color: muted, fontSize: 11);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(leftLabel!, style: endStyle),
            Expanded(child: slider),
            Text(rightLabel!, style: endStyle),
          ],
        ),
        if (labelFor != null)
          Align(
            alignment: Alignment.centerRight,
            child: Text(
              '${labelFor!(values.start)}–${labelFor!(values.end)}',
              style: endStyle,
            ),
          ),
      ],
    );
  }
}
