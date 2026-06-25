import 'package:flutter/material.dart';

import '../../../../core/theme/brand_colors.dart';

/// Renders the per-type parameter UI for an expression. Replaces the
/// previous raw JSON text field — each parameter the firmware accepts now
/// has a labelled slider with the right units and clamp range.
///
/// Parameter keys + valid ranges are pulled from the firmware
/// (`software/lamp-os/src/expressions/*_expression.cpp`):
///   - breathing.breathSpeed      → 1..60 seconds
///   - pulse.pulseSpeed           → 1..10 seconds (total wave travel time)
///   - pulse.cascadeEnabled       → 0/1 (mesh fan-out toggle)
///   - pulse.cascadeStaggerMs     → 0..5000 ms between peers
///   - shifty.shiftDurationMin/Max → 60..1800 seconds (1..30 min)
///   - shifty.fadeDuration         → 10..300 seconds
///   - glitchy.durationMin/Max    → 1..60 frames (≈ 1/30 s each)
///   - glitchy.cascadeEnabled     → 0/1 (mesh fan-out toggle)
///   - glitchy.cascadeStaggerMs   → 0..5000 ms between peers
class ExpressionParamsPanel extends StatelessWidget {
  const ExpressionParamsPanel({
    super.key,
    required this.type,
    required this.parameters,
    required this.onChanged,
    this.devMode = false,
  });

  final String type;

  /// Current parameter map — keyed by the firmware's parameter name.
  final Map<String, int> parameters;

  /// Called with a new map (NOT a mutated copy of the existing map) so the
  /// parent can drive a notifier update.
  final ValueChanged<Map<String, int>> onChanged;

  /// Gates *discovery* of the cascade controls (mesh fan-out toggle +
  /// delay slider). These are power-user controls that live behind the
  /// persisted `devMode` flag — distinct from regular session-only
  /// advanced mode. Once an expression has cascade actively set
  /// (`cascadeEnabled == 1`), the controls stay visible so the user can
  /// edit or disable them without re-enabling devMode. Hidden state
  /// preserves saved values so firmware behaviour persists.
  final bool devMode;

  int _get(String key, int fallback) => parameters[key] ?? fallback;

  void _set(String key, int value) {
    final next = Map<String, int>.from(parameters);
    next[key] = value;
    onChanged(next);
  }

  void _setBoth(String minKey, int minV, String maxKey, int maxV) {
    final next = Map<String, int>.from(parameters);
    next[minKey] = minV;
    next[maxKey] = maxV;
    onChanged(next);
  }

  @override
  Widget build(BuildContext context) {
    return switch (type) {
      'breathing' => _BreathingParams(
          breathSpeed: _get('breathSpeed', 10),
          onBreathSpeed: (v) => _set('breathSpeed', v),
        ),
      'pulse' => _PulseParams(
          pulseSpeed: _get('pulseSpeed', 3),
          onPulseSpeed: (v) => _set('pulseSpeed', v),
          // Cascade is a devMode-only control. Once cascade is actively
          // set on this expression the toggle stays reachable for
          // editing/disabling without re-enabling devMode.
          showCascade: devMode || _get('cascadeEnabled', 0) != 0,
          cascadeEnabled: _get('cascadeEnabled', 0) != 0,
          cascadeStaggerMs: _get('cascadeStaggerMs', 0),
          onCascadeEnabled: (v) => _set('cascadeEnabled', v ? 1 : 0),
          onCascadeStaggerMs: (v) => _set('cascadeStaggerMs', v),
        ),
      'shifty' => _ShiftyParams(
          shiftMin: _get('shiftDurationMin', 300),
          shiftMax: _get('shiftDurationMax', 600),
          fade: _get('fadeDuration', 60),
          onShiftRange: (lo, hi) =>
              _setBoth('shiftDurationMin', lo, 'shiftDurationMax', hi),
          onFade: (v) => _set('fadeDuration', v),
        ),
      'glitchy' => _GlitchyParams(
          durMin: _get('durationMin', 1),
          durMax: _get('durationMax', 3),
          onRange: (lo, hi) =>
              _setBoth('durationMin', lo, 'durationMax', hi),
          // Cascade is a devMode-only control. Once cascade is actively
          // set on this expression the toggle stays reachable for
          // editing/disabling without re-enabling devMode.
          showCascade: devMode || _get('cascadeEnabled', 0) != 0,
          cascadeEnabled: _get('cascadeEnabled', 0) != 0,
          cascadeStaggerMs: _get('cascadeStaggerMs', 0),
          onCascadeEnabled: (v) => _set('cascadeEnabled', v ? 1 : 0),
          onCascadeStaggerMs: (v) => _set('cascadeStaggerMs', v),
        ),
      _ => const SizedBox.shrink(),
    };
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4, top: 8),
      child: Text(
        text,
        style: const TextStyle(color: BrandColors.lampWhite, fontSize: 14),
      ),
    );
  }
}

class _ValueChip extends StatelessWidget {
  const _ValueChip(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 84,
      child: Text(
        text,
        textAlign: TextAlign.right,
        style: const TextStyle(
          color: BrandColors.fogGrey,
          fontSize: 12,
          fontFamily: 'monospace',
        ),
      ),
    );
  }
}

class _ParamSlider extends StatelessWidget {
  const _ParamSlider({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    required this.format,
    this.leftLabel,
    this.rightLabel,
    this.invert = false,
  });

  final String label;
  final int value;
  final int min;
  final int max;
  /// Nullable: pass null to render Material's disabled-Slider state. Used
  /// when a parent toggle disables the parameter but we want it visible so
  /// the user can see what it does.
  final ValueChanged<int>? onChanged;
  final String Function(int) format;
  /// Optional short text below the slider's start (e.g. 'slow', 'short').
  /// Renders only when both labels are non-null.
  final String? leftLabel;
  final String? rightLabel;
  /// When true, the slider visually goes high → low instead of low → high.
  /// Internal storage / `value` stays in the original units; the slider's
  /// thumb position is mirrored. Use for cases where the natural mental
  /// model conflicts with the value direction (e.g. breath cycle length
  /// in seconds — higher value = slower breath, but the user expects
  /// "right = faster").
  final bool invert;

  @override
  Widget build(BuildContext context) {
    final clamped = value.clamp(min, max).toDouble();
    // Slider position runs min..max. If inverted, mirror around the
    // midpoint so the thumb sits on the visually-expected side.
    final sliderValue =
        invert ? (min + max).toDouble() - clamped : clamped;
    final hasEnds = leftLabel != null && rightLabel != null;
    final cb = onChanged;
    final slider = Slider(
      value: sliderValue,
      min: min.toDouble(),
      max: max.toDouble(),
      divisions: max - min,
      onChanged: cb == null
          ? null
          : (v) {
              final raw = v.round();
              final stored = invert ? (min + max) - raw : raw;
              cb(stored);
            },
    );
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionLabel(label),
        if (hasEnds) ...[
          // Match _FrequencySpread's layout: end labels sit inline with
          // the slider, value drops to its own line below. Keeps the
          // editor visually consistent across every expression type.
          Row(
            children: [
              Text(leftLabel!,
                  style: const TextStyle(
                      color: BrandColors.fogGrey, fontSize: 11)),
              Expanded(child: slider),
              Text(rightLabel!,
                  style: const TextStyle(
                      color: BrandColors.fogGrey, fontSize: 11)),
            ],
          ),
          Align(
            alignment: Alignment.centerRight,
            child: Padding(
              padding: const EdgeInsets.only(right: 4),
              child: Text(
                format(value),
                style: const TextStyle(
                  color: BrandColors.fogGrey,
                  fontSize: 12,
                  fontFamily: 'monospace',
                ),
              ),
            ),
          ),
        ] else
          Row(
            children: [
              Expanded(child: slider),
              _ValueChip(format(value)),
            ],
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
    this.leftLabel,
    this.rightLabel,
  });

  final String label;
  final int lo;
  final int hi;
  final int min;
  final int max;
  final void Function(int lo, int hi) onChanged;
  final String Function(int) format;
  final String? leftLabel;
  final String? rightLabel;

  @override
  Widget build(BuildContext context) {
    final loClamp = lo.clamp(min, max);
    final hiClamp = hi.clamp(loClamp, max);
    final hasEnds = leftLabel != null && rightLabel != null;
    final slider = RangeSlider(
      values: RangeValues(loClamp.toDouble(), hiClamp.toDouble()),
      min: min.toDouble(),
      max: max.toDouble(),
      divisions: max - min,
      onChanged: (v) => onChanged(v.start.round(), v.end.round()),
    );
    final valueText = '${format(loClamp)}–${format(hiClamp)}';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SectionLabel(label),
        if (hasEnds) ...[
          Row(
            children: [
              Text(leftLabel!,
                  style: const TextStyle(
                      color: BrandColors.fogGrey, fontSize: 11)),
              Expanded(child: slider),
              Text(rightLabel!,
                  style: const TextStyle(
                      color: BrandColors.fogGrey, fontSize: 11)),
            ],
          ),
          Align(
            alignment: Alignment.centerRight,
            child: Padding(
              padding: const EdgeInsets.only(right: 4),
              child: Text(
                valueText,
                style: const TextStyle(
                  color: BrandColors.fogGrey,
                  fontSize: 12,
                  fontFamily: 'monospace',
                ),
              ),
            ),
          ),
        ] else
          Row(
            children: [
              Expanded(child: slider),
              _ValueChip(valueText),
            ],
          ),
      ],
    );
  }
}

class _BreathingParams extends StatelessWidget {
  const _BreathingParams({
    required this.breathSpeed,
    required this.onBreathSpeed,
  });
  final int breathSpeed;
  final ValueChanged<int> onBreathSpeed;

  @override
  Widget build(BuildContext context) {
    return _ParamSlider(
      label: 'Breath cycle length',
      value: breathSpeed,
      min: 1,
      max: 60,
      onChanged: onBreathSpeed,
      // breathSpeed is the cycle in seconds (1=fast, 60=slow). The user
      // expects "right = faster", so invert the slider visually and use
      // slow/fast end labels to match the rare/often style on the other
      // expressions.
      invert: true,
      leftLabel: 'slow',
      rightLabel: 'fast',
      format: (v) => '${v}s',
    );
  }
}

class _PulseParams extends StatelessWidget {
  const _PulseParams({
    required this.pulseSpeed,
    required this.onPulseSpeed,
    required this.showCascade,
    required this.cascadeEnabled,
    required this.cascadeStaggerMs,
    required this.onCascadeEnabled,
    required this.onCascadeStaggerMs,
  });
  final int pulseSpeed;
  final ValueChanged<int> onPulseSpeed;
  final bool showCascade;
  final bool cascadeEnabled;
  final int cascadeStaggerMs;
  final ValueChanged<bool> onCascadeEnabled;
  final ValueChanged<int> onCascadeStaggerMs;

  @override
  Widget build(BuildContext context) {
    // Slider granularity: 100ms steps over 0..5000ms. _ParamSlider derives
    // divisions from `max - min`, so we scale the wire value (ms) down to
    // 0..50 for the slider and back up on edit. Mirrors Glitchy's cascade
    // slider; see _GlitchyParams for the rationale on always-rendering with
    // a disabled style when the toggle is off.
    String fmtMs(int sliderValue) {
      final ms = sliderValue * 100;
      if (ms < 1000) return '${ms}ms';
      final s = ms / 1000.0;
      final str = s.toStringAsFixed(1);
      return '${str.endsWith('.0') ? s.toInt().toString() : str}s';
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _ParamSlider(
          // pulseSpeed is firmware-side "total travel time" in seconds
          // (1–10s, per pulse_expression.cpp:27). Higher value = slower
          // wave. We invert visually so right = faster and label slow/fast,
          // matching breath cycle's treatment.
          label: 'Pulse speed',
          value: pulseSpeed,
          min: 1,
          max: 10,
          onChanged: onPulseSpeed,
          invert: true,
          leftLabel: 'slow',
          rightLabel: 'fast',
          format: (v) => '${v}s',
        ),
        // Cascade controls only render when advanced mode is unlocked in
        // the current session. Hidden state preserves saved values — the
        // firmware still cascades if cascadeEnabled was set previously.
        if (showCascade) ...[
          Row(
            children: [
              const Expanded(child: _SectionLabel('Cascade to other lamps')),
              Switch(
                value: cascadeEnabled,
                onChanged: onCascadeEnabled,
              ),
            ],
          ),
          _ParamSlider(
            label: 'Delay between lamps',
            value: (cascadeStaggerMs / 100).round().clamp(0, 50),
            min: 0,
            max: 50,
            onChanged: cascadeEnabled
                ? (v) => onCascadeStaggerMs(v * 100)
                : null,
            leftLabel: 'instant',
            rightLabel: 'slow ripple',
            format: fmtMs,
          ),
        ],
      ],
    );
  }
}

class _ShiftyParams extends StatelessWidget {
  const _ShiftyParams({
    required this.shiftMin,
    required this.shiftMax,
    required this.fade,
    required this.onShiftRange,
    required this.onFade,
  });
  final int shiftMin;
  final int shiftMax;
  final int fade;
  final void Function(int, int) onShiftRange;
  final ValueChanged<int> onFade;

  String _fmtMinutes(int seconds) {
    if (seconds < 60) return '${seconds}s';
    final m = seconds ~/ 60;
    final s = seconds % 60;
    if (s == 0) return '${m}m';
    return '${m}m${s}s';
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _RangeParamSlider(
          // "Hold time" is the dwell at peak shifted color. The
          // transition into and out of that color is the separate
          // "Fade duration" slider below. Storage keys
          // (shiftDurationMin/Max) stay unchanged.
          label: 'Hold time',
          lo: shiftMin,
          hi: shiftMax,
          min: 60, // 1 min
          max: 1800, // 30 min
          onChanged: onShiftRange,
          leftLabel: 'short',
          rightLabel: 'long',
          format: _fmtMinutes,
        ),
        _ParamSlider(
          label: 'Fade duration',
          value: fade,
          min: 10,
          max: 300,
          onChanged: onFade,
          leftLabel: 'quick',
          rightLabel: 'slow',
          format: (v) => '${v}s',
        ),
      ],
    );
  }
}

class _GlitchyParams extends StatelessWidget {
  const _GlitchyParams({
    required this.durMin,
    required this.durMax,
    required this.onRange,
    required this.showCascade,
    required this.cascadeEnabled,
    required this.cascadeStaggerMs,
    required this.onCascadeEnabled,
    required this.onCascadeStaggerMs,
  });
  final int durMin;
  final int durMax;
  final void Function(int, int) onRange;
  final bool showCascade;
  final bool cascadeEnabled;
  final int cascadeStaggerMs;
  final ValueChanged<bool> onCascadeEnabled;
  final ValueChanged<int> onCascadeStaggerMs;

  @override
  Widget build(BuildContext context) {
    // Stored as frames (1–60 at 30 fps = 33 ms–2000 ms). Surface the
    // friendlier ms/seconds reading to the user — same wire value, just
    // formatted in real time units.
    String fmt(int frames) {
      final ms = (frames * 1000 / 30).round();
      if (ms < 1000) return '${ms}ms';
      final s = ms / 1000.0;
      // Strip trailing ".0" so "1.0s" reads as "1s".
      final str = s.toStringAsFixed(1);
      return '${str.endsWith('.0') ? s.toInt().toString() : str}s';
    }

    // Slider granularity: 100ms steps over 0..5000ms. _ParamSlider derives
    // divisions from `max - min`, so we scale the wire value (ms) down to
    // 0..50 for the slider and back up on edit. Format uses the same ms/s
    // regime as Glitchy's duration slider above for visual consistency.
    // Note: this slider is the only writer of cascadeStaggerMs and always
    // emits multiples of 100. If a non-multiple ever lands in storage (BLE
    // replay, manual edit), the .round() quantises it on first interaction.
    String fmtMs(int sliderValue) {
      final ms = sliderValue * 100;
      if (ms < 1000) return '${ms}ms';
      final s = ms / 1000.0;
      final str = s.toStringAsFixed(1);
      return '${str.endsWith('.0') ? s.toInt().toString() : str}s';
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _RangeParamSlider(
          label: 'Glitch duration',
          lo: durMin,
          hi: durMax,
          min: 1,
          max: 60,
          onChanged: onRange,
          leftLabel: 'short',
          rightLabel: 'long',
          format: fmt,
        ),
        // Cascade controls only render when advanced mode is unlocked in
        // the current session. Hidden state preserves saved values — the
        // firmware still cascades if cascadeEnabled was set previously.
        if (showCascade) ...[
          // Toggle row uses _SectionLabel for the label so it shares typography
          // and padding with every other section header in this file.
          Row(
            children: [
              const Expanded(child: _SectionLabel('Cascade to other lamps')),
              Switch(
                value: cascadeEnabled,
                onChanged: onCascadeEnabled,
              ),
            ],
          ),
          // Slider stays visible when cascade is off (disabled by passing
          // null onChanged) so users can see what the control does before
          // enabling it. Material Slider renders the disabled style itself.
          _ParamSlider(
            label: 'Delay between lamps',
            value: (cascadeStaggerMs / 100).round().clamp(0, 50),
            min: 0,
            max: 50,
            onChanged: cascadeEnabled
                ? (v) => onCascadeStaggerMs(v * 100)
                : null,
            leftLabel: 'instant',
            rightLabel: 'slow ripple',
            format: fmtMs,
          ),
        ],
      ],
    );
  }
}
