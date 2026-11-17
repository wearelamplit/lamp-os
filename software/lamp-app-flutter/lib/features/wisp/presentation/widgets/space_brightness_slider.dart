import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';
import 'wisp_settings_pending.dart';

/// Top space-dimmer control above the palette gradient bar. Scales every
/// claimed lamp together without touching their colors. Deliberately relative
/// (no absolute %) so the number never reads as a specific lamp brightness.
class SpaceBrightnessSlider extends ConsumerStatefulWidget {
  const SpaceBrightnessSlider({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<SpaceBrightnessSlider> createState() =>
      _SpaceBrightnessSliderState();
}

class _SpaceBrightnessSliderState extends ConsumerState<SpaceBrightnessSlider> {
  // Follows the wisp's value whenever it differs from the slider, except
  // while the slider is held and for a while after release: the write is
  // debounced, so a status from mid-drag can still arrive carrying an
  // intermediate value. The hold ends when a status change brings the
  // released value or after _echoHold, whichever comes first. A rebuild with
  // an unchanged status does not count: setBrightness already wrote the
  // dragged value into the notifier's state, so that is not the wisp's echo.
  static const Duration _echoHold = Duration(milliseconds: 2500);
  late double _value;
  bool _dragging = false;
  DateTime? _holdUntil;

  @override
  void initState() {
    super.initState();
    _seed();
  }

  // Never seeds from placeholders: the slider shows pending until configKnown.
  @override
  void didUpdateWidget(SpaceBrightnessSlider old) {
    super.didUpdateWidget(old);
    final s = widget.status;
    if (!s.configKnown || _dragging) return;
    final shown = s.brightness == _value.round();
    final hold = _holdUntil;
    if (hold != null) {
      final echoed = shown && old.status.brightness != s.brightness;
      if (!echoed && DateTime.now().isBefore(hold)) return;
      _holdUntil = null;
    }
    if (!shown) _seed();
  }

  void _seed() {
    _value = widget.status.brightness.clamp(0, 100).toDouble();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    return Material(
      color: theme.colorScheme.surface,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(
            AppSpace.lg, AppSpace.sm, AppSpace.lg, AppSpace.sm),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.brightness_6_outlined,
                    size: 18, color: theme.colorScheme.secondary),
                const SizedBox(width: AppSpace.sm),
                Text('Relative Brightness',
                    style: theme.textTheme.titleSmall),
              ],
            ),
            Text(
              'Dims all lamps in this space together.',
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
            if (!widget.status.configKnown)
              WispSettingsPending(lampId: widget.lampId)
            else
              Slider(
                min: 0,
                max: 100,
                value: _value,
                label: _value >= 99.5 ? 'Full' : 'Dimmer',
                onChangeStart: (_) => _dragging = true,
                onChanged: (v) {
                  setState(() => _value = v);
                  notifier.setBrightness(v.round());
                },
                onChangeEnd: (_) {
                  _dragging = false;
                  _holdUntil = DateTime.now().add(_echoHold);
                  notifier.flushBrightness();
                },
              ),
          ],
        ),
      ),
    );
  }
}
