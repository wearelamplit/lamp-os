import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/utils/duration_format.dart';
import '../../../../core/widgets/lamp_card.dart';
import '../../../../core/widgets/settings_row.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';
import 'wisp_settings_pending.dart';

/// Maps a slider position [0..1] to milliseconds [30000..3600000]
/// on a logarithmic scale (base 120). p=0 → 30 s, p=1 → 60 min.
int posToMs(double p) =>
    (30000 * pow(120.0, p)).round().clamp(30000, 3600000);

/// Inverse of [posToMs]: milliseconds → slider position [0..1].
double msToPos(int ms) => log(ms / 30000.0) / log(120.0);

/// Two-slider drift panel. Pass the wisp's current [status] for initial
/// slider positions and [lampId] to route writes.
class DriftControls extends ConsumerStatefulWidget {
  const DriftControls({
    super.key,
    required this.lampId,
    required this.status,
  });

  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<DriftControls> createState() => _DriftControlsState();
}

class _DriftControlsState extends ConsumerState<DriftControls> {
  // Follows the wisp's values whenever they differ from the sliders, except
  // while a slider is held and for a while after release: the writes are
  // debounced, so a status from mid-drag can still arrive carrying an
  // intermediate value. The hold ends when the wisp echoes the released
  // value or after _echoHold, whichever comes first.
  static const Duration _echoHold = Duration(milliseconds: 2500);
  late double _pos;
  late int _fadePct;
  bool _dragging = false;
  DateTime? _holdUntil;

  @override
  void initState() {
    super.initState();
    _seed();
  }

  // Never seeds from placeholders: the form shows pending until configKnown.
  @override
  void didUpdateWidget(DriftControls old) {
    super.didUpdateWidget(old);
    final s = widget.status;
    if (!s.configKnown || _dragging) return;
    final shown =
        s.driftIntervalMs == posToMs(_pos) && s.driftFadePct == _fadePct;
    final hold = _holdUntil;
    if (hold != null) {
      if (!shown && DateTime.now().isBefore(hold)) return;
      _holdUntil = null;
    }
    if (!shown) _seed();
  }

  void _dragStart(double _) => _dragging = true;

  void _dragEnd(double _) {
    _dragging = false;
    _holdUntil = DateTime.now().add(_echoHold);
    ref.read(wispNotifierProvider(widget.lampId).notifier).flushDrift();
  }

  void _seed() {
    _pos = msToPos(widget.status.driftIntervalMs).clamp(0.0, 1.0);
    _fadePct = widget.status.driftFadePct.clamp(0, 100);
  }

  String _intervalLabel() =>
      formatDuration(Duration(milliseconds: posToMs(_pos)));

  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SettingsGroupHeading('Color drift'),
        if (!widget.status.configKnown)
          LampCard(child: WispSettingsPending(lampId: widget.lampId))
        else
          LampCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                _LabelRow(left: 'How often', right: _intervalLabel()),
                Slider(
                  min: 0,
                  max: 1,
                  value: _pos,
                  onChangeStart: _dragStart,
                  onChanged: (v) {
                    setState(() => _pos = v);
                    notifier.setDrift(posToMs(v), _fadePct);
                  },
                  onChangeEnd: _dragEnd,
                ),
                const SizedBox(height: AppSpace.lg),
                _LabelRow(left: 'Fade', right: '$_fadePct%'),
                Slider(
                  min: 0,
                  max: 100,
                  divisions: 100,
                  value: _fadePct.toDouble(),
                  onChangeStart: _dragStart,
                  onChanged: (v) {
                    setState(() => _fadePct = v.round());
                    notifier.setDrift(posToMs(_pos), v.round());
                  },
                  onChangeEnd: _dragEnd,
                ),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text(
                        'Snappy',
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                              color:
                                  Theme.of(context).colorScheme.onSurfaceVariant,
                            ),
                      ),
                      Text(
                        'Flowing',
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                              color:
                                  Theme.of(context).colorScheme.onSurfaceVariant,
                            ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
      ],
    );
  }
}

class _LabelRow extends StatelessWidget {
  const _LabelRow({required this.left, required this.right});

  final String left;
  final String right;

  @override
  Widget build(BuildContext context) {
    final muted = Theme.of(context).textTheme.bodySmall?.copyWith(
          color: Theme.of(context).colorScheme.onSurfaceVariant,
        );
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: AppSpace.md),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(left, style: muted),
          Text(right, style: muted),
        ],
      ),
    );
  }
}
