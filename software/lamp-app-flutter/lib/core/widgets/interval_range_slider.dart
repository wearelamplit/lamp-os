import 'package:flutter/material.dart';

/// A themed dual-grab range slider for interval bounds.
///
/// Wraps Material [RangeSlider] with optional thumb labels via [labelFor].
/// Colors inherit from [SliderTheme] / the ambient [ColorScheme]; no
/// hardcoded colors.
class IntervalRangeSlider extends StatelessWidget {
  const IntervalRangeSlider({
    required this.values,
    required this.min,
    required this.max,
    required this.onChanged,
    this.minGap = 0,
    this.labelFor,
    this.leftLabel,
    this.rightLabel,
    super.key,
  });

  final RangeValues values;
  final double min;
  final double max;

  /// Minimum spread between the thumbs; the dragged thumb pushes the other so
  /// the pair never closes within [minGap]. 0 = free.
  final double minGap;
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
    final display = _withGap(values);
    final slider = RangeSlider(
      values: display,
      min: min,
      max: max,
      onChanged: (v) => onChanged(_withGap(v, previous: display)),
      labels: labelFor == null
          ? null
          : RangeLabels(labelFor!(display.start), labelFor!(display.end)),
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
              '${labelFor!(display.start)}–${labelFor!(display.end)}',
              style: endStyle,
            ),
          ),
      ],
    );
  }

  /// Enforce [minGap] between the thumbs. On drag ([previous] set) the moved
  /// thumb pushes the other; on initial load ([previous] null) the high thumb
  /// is nudged up to keep the spread.
  RangeValues _withGap(RangeValues v, {RangeValues? previous}) {
    if (minGap <= 0 || v.end - v.start >= minGap) {
      return RangeValues(v.start, v.end.clamp(v.start, max));
    }
    if (previous != null && v.start != previous.start) {
      final end = (v.start + minGap).clamp(min, max);
      return RangeValues((end - minGap).clamp(min, max), end);
    }
    if (previous != null && v.end != previous.end) {
      return RangeValues((v.end - minGap).clamp(min, max), v.end);
    }
    return RangeValues(v.start, (v.start + minGap).clamp(min, max));
  }
}
