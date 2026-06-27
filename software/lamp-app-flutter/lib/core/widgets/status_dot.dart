import 'package:flutter/material.dart';

import '../theme/brand_extras.dart';

enum StatusKind { offline, bluetooth, mesh, searching }

class StatusDot extends StatefulWidget {
  const StatusDot({super.key, required this.kind, this.size = 10});

  final StatusKind kind;
  final double size;

  @override
  State<StatusDot> createState() => _StatusDotState();
}

class _StatusDotState extends State<StatusDot>
    with SingleTickerProviderStateMixin {
  late final AnimationController _ctrl;

  @override
  void initState() {
    super.initState();
    _ctrl = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    );
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // Pause the pulse when this widget is in an off-screen tab. Flutter's
    // TickerMode is `false` for descendants of a NavigationBar destination
    // that isn't currently visible; respecting it keeps a row of dots from
    // burning CPU when their lamp shell tab is hidden.
    if (TickerMode.getValuesNotifier(context).value.enabled) {
      if (!_ctrl.isAnimating) _ctrl.repeat(reverse: true);
    } else {
      _ctrl.stop();
    }
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // Searching is the "we've started scanning but haven't heard back yet"
    // state — render an indeterminate spinner rather than a dot so the
    // user can distinguish it from a true Offline.
    final colorScheme = Theme.of(context).colorScheme;
    if (widget.kind == StatusKind.searching) {
      return Semantics(
        label: 'Searching',
        child: SizedBox(
          width: widget.size,
          height: widget.size,
          child: CircularProgressIndicator(
            strokeWidth: (widget.size * 0.18).clamp(1.5, 3.0),
            valueColor: AlwaysStoppedAnimation<Color>(
              colorScheme.onSurfaceVariant.withValues(alpha: 0.85),
            ),
          ),
        ),
      );
    }

    // Mesh-connected lamps glow brand green (mesh is the live, healthy
    // state). BT-only is tertiary at ~70 % so it reads as "online but not
    // the live link." Offline is a dimmed grey — must read as more muted
    // than bluetooth so the inactive state isn't louder than the active one.
    final color = switch (widget.kind) {
      StatusKind.offline => colorScheme.onSurfaceVariant.withValues(alpha: 0.35),
      StatusKind.bluetooth => colorScheme.tertiary.withValues(alpha: 0.7),
      StatusKind.mesh => context.brandExtras.success,
      StatusKind.searching =>
        colorScheme.onSurfaceVariant.withValues(alpha: 0.35), // unreachable
    };

    // Screen-readers see only the visual dot otherwise — name it (audit
    // W8): "Mesh connected" / "Bluetooth only" / "Offline".
    final semanticsLabel = switch (widget.kind) {
      StatusKind.offline => 'Offline',
      StatusKind.bluetooth => 'Bluetooth only',
      StatusKind.mesh => 'Mesh connected',
      StatusKind.searching => 'Searching', // unreachable
    };

    return Semantics(
      label: semanticsLabel,
      child: AnimatedBuilder(
        animation: _ctrl,
        builder: (context, _) {
          final glow = widget.kind == StatusKind.mesh
              ? 6 + _ctrl.value * 8
              : (widget.kind == StatusKind.bluetooth ? 4.0 : 0.0);
          return Container(
            width: widget.size,
            height: widget.size,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: color,
              boxShadow: glow > 0
                  ? [
                      BoxShadow(
                        color: color.withValues(alpha: 0.6),
                        blurRadius: glow,
                      ),
                    ]
                  : const [],
            ),
          );
        },
      ),
    );
  }
}
