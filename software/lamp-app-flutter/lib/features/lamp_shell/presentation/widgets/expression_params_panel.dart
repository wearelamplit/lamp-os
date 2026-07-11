import 'package:collection/collection.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../domain/expression_catalog.dart';

/// Renders an expression's editor controls generically from its firmware
/// [ExpressionDescriptor]. Every control (sliders, ranges, segmented enums,
/// the zone, the whole-strip toggle) is derived from the catalog; the only
/// hardcoded, non-schema pieces are the client conventions the firmware reads
/// but doesn't declare: the `fullStrip` zone flag and the mesh `cascade`
/// controls (dev-mode, non-continuous expressions only).
///
/// Edits merge-patch the params map: each change copies the map and sets only
/// the touched keys, so keys the schema-driven UI never surfaced (e.g. a zone
/// hidden behind a toggle) survive round-trips instead of being dropped.
class ExpressionParamsPanel extends StatelessWidget {
  const ExpressionParamsPanel({
    super.key,
    required this.descriptor,
    required this.parameters,
    required this.onChanged,
    required this.pixelCount,
    required this.intervalMin,
    required this.intervalMax,
    required this.onIntervalChanged,
    this.devMode = false,
    this.onZonePreview,
    this.onZonePreviewEnd,
  });

  final ExpressionDescriptor descriptor;

  /// Current params map, keyed by the firmware's parameter name.
  final Map<String, int> parameters;

  /// Called with a fresh map (never a mutated alias) so the parent drives a
  /// notifier update.
  final ValueChanged<Map<String, int>> onChanged;

  /// Pixel count for the target strip (max of shade/base for target=both).
  /// Resolves pixel-relative bounds (zone, count/size upper limits).
  final int pixelCount;

  /// Top-level instance interval, distinct from the params map.
  final int intervalMin;
  final int intervalMax;
  final void Function(int min, int max) onIntervalChanged;

  /// Gates discovery of the mesh cascade controls behind the persisted
  /// devMode flag. Once cascade is actively set the controls stay visible so
  /// the user can edit/disable them without re-enabling devMode.
  final bool devMode;

  final void Function(int posMin, int posMax)? onZonePreview;
  final VoidCallback? onZonePreviewEnd;

  int _get(String key, int fallback) => parameters[key] ?? fallback;

  void _set(String key, int value) {
    onChanged(Map<String, int>.from(parameters)..[key] = value);
  }

  void _setBoth(String minKey, int minV, String maxKey, int maxV) {
    onChanged(Map<String, int>.from(parameters)
      ..[minKey] = minV
      ..[maxKey] = maxV);
  }

  CatalogParam? get _zoningEnum => descriptor.params.firstWhereOrNull((p) =>
      p.type == ParamType.enumeration && p.options.any((o) => o.zoning));

  /// Whether zone-gated controls (the zone range + `requiresZoning` params)
  /// are active. A zoning enum takes precedence; else an optional zone is
  /// driven by the `fullStrip` toggle; else a plain zone is always active.
  bool get _zoningActive {
    final ze = _zoningEnum;
    if (ze != null) {
      final sel = _get(ze.key, ze.def.resolve(pixelCount));
      return ze.options.firstWhereOrNull((o) => o.value == sel)?.zoning ?? false;
    }
    if (descriptor.hasZone && descriptor.zoneOptional) {
      return _get('fullStrip', 1) == 0;
    }
    return descriptor.hasZone;
  }

  @override
  Widget build(BuildContext context) {
    final zoning = _zoningActive;
    final children = <Widget>[];

    if (descriptor.hasZone && descriptor.zoneOptional) {
      children.add(_segmented(
        label: 'Mode',
        options: const ['Whole strip', 'Region'],
        selectedIndex: _get('fullStrip', 1) == 1 ? 0 : 1,
        onIndex: (i) => _set('fullStrip', i == 0 ? 1 : 0),
      ));
    }

    for (final p in descriptor.params) {
      if (p.type != ParamType.enumeration) continue;
      children.add(_segmented(
        label: p.label,
        options: p.options.map((o) => o.label).toList(),
        selectedIndex: p.options
            .indexWhere((o) => o.value == _get(p.key, p.def.resolve(pixelCount)))
            .clamp(0, p.options.length - 1),
        onIndex: (i) => _set(p.key, p.options[i].value),
      ));
    }

    if (zoning && descriptor.hasZone) {
      children.add(const SectionHeader('Placement'));
      children.add(_RangeParamSlider(
        label: 'Zone',
        lo: _get('posMin', 0),
        hi: _get('posMax', pixelCount - 1),
        min: 0,
        max: pixelCount - 1 < 0 ? 0 : pixelCount - 1,
        onChanged: (lo, hi) => _setBoth('posMin', lo, 'posMax', hi),
        onPreview: onZonePreview,
        onChangeEnd: onZonePreviewEnd,
        format: (v) => '$v',
      ));
    }

    for (final p in descriptor.params) {
      if (p.type == ParamType.enumeration) continue;
      if (p.requiresZoning && !zoning) continue;
      final maxV = p.max.resolve(pixelCount);
      children.add(_ParamSlider(
        label: p.label,
        value: _get(p.key, p.def.resolve(pixelCount)),
        min: p.min,
        max: maxV < p.min ? p.min : maxV,
        step: p.step,
        onChanged: (v) => _set(p.key, v),
        invert: p.invert,
        leftLabel: p.leftLabel,
        rightLabel: p.rightLabel,
        format: (v) => _fmtUnit(v, p.unit),
      ));
    }

    final duration = descriptor.duration;
    if (duration != null && duration.minKey != null && duration.maxKey != null) {
      children.add(_RangeParamSlider(
        label: duration.label ?? 'Duration',
        lo: _get(duration.minKey!, duration.defLo),
        hi: _get(duration.maxKey!, duration.defHi),
        min: duration.min,
        max: duration.max,
        step: duration.step,
        onChanged: (lo, hi) =>
            _setBoth(duration.minKey!, lo, duration.maxKey!, hi),
        leftLabel: 'short',
        rightLabel: 'long',
        format: (v) => _fmtUnit(v, duration.unit),
      ));
    }

    final interval = descriptor.interval;
    if (interval != null) {
      children.add(const SectionHeader('Trigger interval'));
      children.add(_RangeParamSlider(
        label: interval.label ?? 'Interval',
        lo: intervalMin,
        hi: intervalMax,
        min: interval.min,
        max: interval.max,
        step: interval.step,
        onChanged: onIntervalChanged,
        leftLabel: 'often',
        rightLabel: 'rare',
        format: (v) => _fmtUnit(v, interval.unit),
      ));
    }

    if (!descriptor.continuous &&
        (devMode || _get('cascadeEnabled', 0) != 0)) {
      final cascadeOn = _get('cascadeEnabled', 0) != 0;
      children.add(Row(
        children: [
          const Expanded(child: _SectionLabel('Cascade to other lamps')),
          Switch(
            value: cascadeOn,
            onChanged: (v) => _set('cascadeEnabled', v ? 1 : 0),
          ),
        ],
      ));
      children.add(_ParamSlider(
        label: 'Delay between lamps',
        value: (_get('cascadeStaggerMs', 0) / 100).round().clamp(0, 50),
        min: 0,
        max: 50,
        step: 1,
        onChanged: cascadeOn ? (v) => _set('cascadeStaggerMs', v * 100) : null,
        leftLabel: 'instant',
        rightLabel: 'slow ripple',
        format: (v) => _fmtUnit(v * 100, 'ms'),
      ));
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: children,
    );
  }
}

String _fmtUnit(int value, String? unit) {
  switch (unit) {
    case 's':
      if (value < 90) return '${value}s';
      final m = value ~/ 60;
      final s = value % 60;
      return s == 0 ? '${m}m' : '${m}m${s}s';
    case 'ms':
      if (value < 1000) return '${value}ms';
      final str = (value / 1000.0).toStringAsFixed(1);
      return '${str.endsWith('.0') ? (value ~/ 1000).toString() : str}s';
    case '%':
      return '$value%';
    default:
      return '$value';
  }
}

Widget _segmented({
  required String label,
  required List<String> options,
  required int selectedIndex,
  required ValueChanged<int> onIndex,
}) =>
    Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _SectionLabel(label),
        SegmentedButton<int>(
          showSelectedIcon: false,
          segments: [
            for (var i = 0; i < options.length; i++)
              ButtonSegment(value: i, label: Text(options[i])),
          ],
          selected: {selectedIndex},
          onSelectionChanged: (s) => onIndex(s.first),
        ),
      ],
    );

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpace.xs, top: AppSpace.sm),
      child: Text(
        text,
        style: TextStyle(
            color: Theme.of(context).colorScheme.onSurface, fontSize: 14),
      ),
    );
  }
}

/// Snap [raw] to the nearest [step] within [min]..[max].
int _snap(int raw, int min, int max, int step) {
  if (step <= 1) return raw.clamp(min, max);
  final snapped = min + ((raw - min) / step).round() * step;
  return snapped.clamp(min, max);
}

int _divisions(int min, int max, int step) {
  if (max <= min) return 1;
  final d = ((max - min) / (step <= 0 ? 1 : step)).round();
  return d < 1 ? 1 : d;
}

class _ParamSlider extends StatelessWidget {
  const _ParamSlider({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    required this.format,
    this.step = 1,
    this.leftLabel,
    this.rightLabel,
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

  /// Mirrors the thumb high↔low while storage stays in original units. Used
  /// where "right = faster" conflicts with the stored value's direction.
  final bool invert;

  @override
  Widget build(BuildContext context) {
    final clamped = value.clamp(min, max).toDouble();
    final sliderValue = invert ? (min + max).toDouble() - clamped : clamped;
    final hasEnds = leftLabel != null && rightLabel != null;
    final cb = onChanged;
    final slider = Slider(
      value: sliderValue.clamp(min.toDouble(), max.toDouble()),
      min: min.toDouble(),
      max: max.toDouble(),
      divisions: _divisions(min, max, step),
      onChanged: cb == null
          ? null
          : (v) {
              final raw = _snap(v.round(), min, max, step);
              cb(invert ? (min + max) - raw : raw);
            },
    );
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionLabel(label),
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
              format(value),
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

class _RangeParamSlider extends StatelessWidget {
  const _RangeParamSlider({
    required this.label,
    required this.lo,
    required this.hi,
    required this.min,
    required this.max,
    required this.onChanged,
    required this.format,
    this.step = 1,
    this.leftLabel,
    this.rightLabel,
    this.onPreview,
    this.onChangeEnd,
  });

  final String label;
  final int lo;
  final int hi;
  final int min;
  final int max;
  final int step;
  final void Function(int lo, int hi) onChanged;
  final String Function(int) format;
  final String? leftLabel;
  final String? rightLabel;
  final void Function(int lo, int hi)? onPreview;
  final VoidCallback? onChangeEnd;

  @override
  Widget build(BuildContext context) {
    final loClamp = lo.clamp(min, max);
    final hiClamp = hi.clamp(loClamp, max);
    final hasEnds = leftLabel != null && rightLabel != null;
    final slider = RangeSlider(
      values: RangeValues(loClamp.toDouble(), hiClamp.toDouble()),
      min: min.toDouble(),
      max: max.toDouble(),
      divisions: _divisions(min, max, step),
      onChanged: (v) {
        final a = _snap(v.start.round(), min, max, step);
        final b = _snap(v.end.round(), min, max, step);
        onChanged(a, b);
        onPreview?.call(a, b);
      },
      onChangeEnd: onChangeEnd == null ? null : (_) => onChangeEnd!(),
    );
    final valueText = '${format(loClamp)}–${format(hiClamp)}';
    final muted = Theme.of(context).colorScheme.onSurfaceVariant;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionLabel(label),
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
              valueText,
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
