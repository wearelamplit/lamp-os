import 'package:collection/collection.dart';
import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/lamp_card.dart';
import '../../../../core/widgets/settings_row.dart';
import '../../domain/expression_catalog.dart';
import 'motion_picker.dart';

/// Which slice of the panel to render. The editor emits [placement] directly
/// under the Target chooser and [main] below the Colors section, so Placement
/// sits above Colors while the rest stays below.
enum ExpressionPanelPart { placement, main }

/// Renders an expression's editor controls generically from its firmware
/// [ExpressionDescriptor]. Every control (sliders, ranges, segmented enums,
/// the zone, the whole-strip toggle) is derived from the catalog; the only
/// hardcoded, non-schema pieces are the client conventions the firmware reads
/// but doesn't declare: the `fullStrip` zone flag and the mesh `cascade`
/// controls (dev-mode, non-continuous expressions only).
///
/// Controls are sorted into yellow-headed cards (Placement / Timing /
/// Behaviour / Mesh); empty groups don't render. [part] selects which cards
/// this instance emits.
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
    this.part = ExpressionPanelPart.main,
    this.devMode = false,
    this.onZonePreview,
    this.onZonePreviewEnd,
  });

  final ExpressionPanelPart part;

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

    final options = <Widget>[];
    final placement = <Widget>[];
    final timing = <Widget>[];
    final mesh = <Widget>[];
    final motion = <Widget>[];

    if (descriptor.hasZone && descriptor.zoneOptional) {
      placement.add(_segmented(
        label: 'Mode',
        options: const ['Whole strip', 'Region'],
        selectedIndex: _get('fullStrip', 1) == 1 ? 0 : 1,
        onIndex: (i) => _set('fullStrip', i == 0 ? 1 : 0),
        help: _zoneHelp,
      ));
    }

    for (final p in descriptor.params) {
      if (p.type != ParamType.enumeration || p.options.isEmpty) continue;
      if (p.key == kEasingParamKey) {
        motion.add(MotionPicker(
          label: p.label,
          options: p.options,
          value: _get(p.key, p.def.resolve(pixelCount)),
          onChanged: (v) => _set(p.key, v),
        ));
        continue;
      }
      final control = _segmented(
        label: p.label,
        options: p.options.map((o) => o.label).toList(),
        selectedIndex: p.options
            .indexWhere((o) => o.value == _get(p.key, p.def.resolve(pixelCount)))
            .clamp(0, p.options.length - 1),
        onIndex: (i) => _set(p.key, p.options[i].value),
      );
      // Loop rides in the Motion card with the easing picker; other enums
      // stay in Behaviour.
      (p.key == kLoopParamKey ? motion : options).add(control);
    }

    if (zoning && descriptor.hasZone) {
      placement.add(_RangeParamSlider(
        label: 'Zone',
        lo: _get('posMin', 0),
        hi: _get('posMax', pixelCount - 1),
        min: 0,
        max: pixelCount - 1 < 0 ? 0 : pixelCount - 1,
        onChanged: (lo, hi) => _setBoth('posMin', lo, 'posMax', hi),
        onPreview: onZonePreview,
        onChangeEnd: onZonePreviewEnd,
        help: descriptor.zoneOptional ? null : _zoneHelp,
        format: (v) => '$v',
      ));
    }

    for (final p in descriptor.params) {
      if (p.type == ParamType.enumeration) continue;
      if (p.requiresZoning && !zoning) continue;
      final maxV = p.max.resolve(pixelCount);
      options.add(_ParamSlider(
        label: p.label,
        value: _get(p.key, p.def.resolve(pixelCount)),
        min: p.min,
        max: maxV < p.min ? p.min : maxV,
        step: p.step,
        onChanged: (v) => _set(p.key, v),
        invert: p.invert,
        leftLabel: p.leftLabel,
        rightLabel: p.rightLabel,
        help: p.help,
        format: (v) => _fmtUnit(v, p.unit),
      ));
    }

    final duration = descriptor.duration;
    if (duration != null && duration.minKey != null && duration.maxKey != null) {
      timing.add(_RangeParamSlider(
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
        help: duration.help,
        format: (v) => _fmtUnit(v, duration.unit),
      ));
    }

    // A continuous (e.g. looped) expression never re-triggers on an interval,
    // so hide the control when it would do nothing.
    final interval = descriptor.interval;
    if (interval != null && !effectiveContinuous(descriptor, parameters)) {
      timing.add(_RangeParamSlider(
        label: interval.label ?? 'Interval',
        lo: intervalMin,
        hi: intervalMax,
        min: interval.min,
        max: interval.max,
        step: interval.step,
        onChanged: onIntervalChanged,
        leftLabel: 'often',
        rightLabel: 'rare',
        help: interval.help,
        format: (v) => _fmtUnit(v, interval.unit),
      ));
    }

    if (!effectiveContinuous(descriptor, parameters) &&
        (devMode || _get('cascadeEnabled', 0) != 0)) {
      final cascadeOn = _get('cascadeEnabled', 0) != 0;
      mesh.add(Row(
        children: [
          Expanded(
            child: Builder(
              builder: (context) => Text(
                'Cascade to other lamps',
                style: Theme.of(context).textTheme.bodyMedium,
              ),
            ),
          ),
          Switch(
            value: cascadeOn,
            onChanged: (v) => _set('cascadeEnabled', v ? 1 : 0),
          ),
        ],
      ));
      mesh.add(_ParamSlider(
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

    final children = switch (part) {
      ExpressionPanelPart.placement => [
          ..._group('Placement', placement),
          ..._group('Motion', motion),
        ],
      ExpressionPanelPart.main => [
          ..._group('Timing', timing),
          ..._group('Behaviour', options),
          ..._group('Mesh', mesh),
        ],
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: children,
    );
  }

  List<Widget> _group(String title, List<Widget> children) {
    if (children.isEmpty) return const [];
    return [
      SettingsGroupHeading(title),
      LampCard(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            for (var i = 0; i < children.length; i++) ...[
              if (i > 0) const SizedBox(height: AppSpace.lg),
              children[i],
            ],
          ],
        ),
      ),
    ];
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

const _zoneHelp =
    'Restricts the effect to a section of the strip rather than its full length.';

Widget _controlLabel(BuildContext context, String text) => Padding(
      padding: const EdgeInsets.only(bottom: AppSpace.xs),
      child: Text(text, style: Theme.of(context).textTheme.bodyMedium),
    );

Widget _helpText(BuildContext context, String text) => Text(
      text,
      style: TextStyle(
        color: Theme.of(context).colorScheme.onSurfaceVariant,
        fontSize: 11,
      ),
    );

Widget _segmented({
  required String label,
  required List<String> options,
  required int selectedIndex,
  required ValueChanged<int> onIndex,
  String? help,
}) =>
    Builder(
      builder: (context) => Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _controlLabel(context, label),
          if (help != null)
            Padding(
              padding: const EdgeInsets.only(bottom: AppSpace.xs),
              child: _helpText(context, help),
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
      ),
    );

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
        _controlLabel(context, label),
        if (help != null) _helpText(context, help!),
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
    this.help,
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
  final String? help;
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
        _controlLabel(context, label),
        if (help != null) _helpText(context, help!),
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
