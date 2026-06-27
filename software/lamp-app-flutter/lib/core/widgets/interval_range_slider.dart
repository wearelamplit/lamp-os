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
    super.key,
  });

  final RangeValues values;
  final double min;
  final double max;
  final ValueChanged<RangeValues> onChanged;

  /// Optional formatter applied to each thumb value for the popup label.
  /// Receives the raw slider value; returns the display string.
  final String Function(double)? labelFor;

  @override
  Widget build(BuildContext context) {
    return RangeSlider(
      values: values,
      min: min,
      max: max,
      onChanged: onChanged,
      labels: labelFor == null
          ? null
          : RangeLabels(labelFor!(values.start), labelFor!(values.end)),
    );
  }
}
