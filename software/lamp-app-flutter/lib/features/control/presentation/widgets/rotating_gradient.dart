import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../../domain/color_rotation.dart';
import '../../domain/lamp_color.dart';

/// A vertical closed-loop gradient of [colors] (last back to first) that
/// slides as the lamp's rotation does: Up toward the top of the box, Down
/// toward the bottom, one loop per period. Holds still at its start under the
/// OS reduce-motion setting.
class RotatingGradient extends StatefulWidget {
  const RotatingGradient({
    super.key,
    required this.colors,
    required this.rotation,
    required this.stop0AtBottom,
    this.borderRadius,
  });

  final List<LampColor> colors;
  final ColorRotation rotation;

  /// Which end holds the first colour at rest: the bottom for a standard
  /// lamp's shade, the top for everything else.
  final bool stop0AtBottom;
  final BorderRadius? borderRadius;

  @override
  State<RotatingGradient> createState() => _RotatingGradientState();
}

class _RotatingGradientState extends State<RotatingGradient>
    with SingleTickerProviderStateMixin {
  late final Ticker _ticker = createTicker(_tick);
  Duration _lastTick = Duration.zero;
  double _moved = 0;

  void _tick(Duration elapsed) {
    final dt = elapsed - _lastTick;
    _lastTick = elapsed;
    final r = widget.rotation;
    setState(() => _moved = advanceRotation(_moved, dt, r.periodS, r.direction));
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final still = MediaQuery.maybeDisableAnimationsOf(context) ?? false;
    if (still && _ticker.isActive) {
      _ticker.stop();
      _moved = 0;
    } else if (!still && !_ticker.isActive) {
      _lastTick = Duration.zero;
      _ticker.start();
    }
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: BoxDecoration(
        borderRadius: widget.borderRadius,
        gradient: _loopGradient(
          widget.colors,
          stop0AtBottom: widget.stop0AtBottom,
          movedUp: _moved,
        ),
      ),
    );
  }
}

/// The closed loop of [colors] spanning the box once, shifted toward the top
/// by [movedUp] of a loop and tiled so the shift wraps.
LinearGradient _loopGradient(
  List<LampColor> colors, {
  required bool stop0AtBottom,
  required double movedUp,
}) {
  final swatches = colors.isEmpty
      ? const [Colors.black]
      : colors.map((c) => c.toSwatch()).toList();
  // Alignment spans -1..1, so one loop is 2 units and moving up is negative y.
  final shift = -2 * movedUp;
  final top = Alignment(0, -1 + shift);
  final bottom = Alignment(0, 1 + shift);
  return LinearGradient(
    begin: stop0AtBottom ? bottom : top,
    end: stop0AtBottom ? top : bottom,
    colors: [...swatches, swatches.first],
    tileMode: TileMode.repeated,
  );
}
