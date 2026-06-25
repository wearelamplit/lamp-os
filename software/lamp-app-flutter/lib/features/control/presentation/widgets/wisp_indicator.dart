import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../../core/routing/routes.dart';
import '../../../../core/utils/tap_counter.dart';
import '../../../wisp/application/wisp_notifier.dart';
import '../../application/advanced_session.dart';
import '../../domain/lamp_color.dart';

/// Whimsical will-o'-wisp indicator: two softly glowing orbs that gently
/// pulse + float, tinted by whatever the wisp is currently painting on
/// the lamp's Base + Shade. Renders when:
///   - Wisp is actively controlling at least one surface (default), OR
///   - Advanced settings are unlocked AND the wisp is on the mesh
///     (`status.present`). The orbs use `status.offColor` for the tint
///     because the wisp isn't painting per-surface colors right now —
///     this gives operators an entry point to the wisp config even when
///     the wisp is in Off mode or stuck waiting for Aurora to publish a
///     zone. Without this escape hatch, the 5-tap-orbs gesture would be
///     inaccessible whenever the wisp wasn't painting.
///
/// Sized to sit in the empty top-right of the control screen header. No
/// label — the user asked for whimsy without explanation.
class WispIndicator extends ConsumerStatefulWidget {
  const WispIndicator({
    super.key,
    required this.lampId,
    this.size = 56,
  });

  final String lampId;
  final double size;

  @override
  ConsumerState<WispIndicator> createState() => _WispIndicatorState();
}

class _WispIndicatorState extends ConsumerState<WispIndicator>
    with TickerProviderStateMixin {
  late final AnimationController _drift;
  late final AnimationController _pulse;
  // 5-tap gesture unlocks the wisp config route — same pattern as the
  // Lamplit-wordmark advanced-unlock in info_screen.dart. No visible
  // affordance; users discover via word-of-mouth or by accident.
  late final TapCounter _tap;

  @override
  void initState() {
    super.initState();
    _drift = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 6),
    )..repeat();
    _pulse = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1900),
    )..repeat(reverse: true);
    _tap = TapCounter(
      count: 5,
      window: const Duration(seconds: 3),
      onTriggered: () {
        if (!mounted) return;
        GoRouter.maybeOf(context)?.push(AppRoutes.wispConfig(widget.lampId));
      },
    );
  }

  @override
  void dispose() {
    _drift.dispose();
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final status = ref.watch(
      wispNotifierProvider(widget.lampId).select(
        (async) => async.value,
      ),
    );
    // Advanced-mode escape hatch: even when the wisp isn't actively
    // painting, surface the orbs so the 5-tap gesture remains reachable.
    // Required for the operator to flip a stuck-in-Off-mode wisp back
    // into Manual or Aurora without app-restart workarounds.
    final advanced = ref.watch(effectiveAdvancedProvider(widget.lampId));
    if (status == null) {
      return SizedBox(width: widget.size, height: widget.size);
    }
    final controlling = status.controlling;
    final showAsEscapeHatch = !controlling && advanced && status.present;
    if (!controlling && !showAsEscapeHatch) {
      return SizedBox(width: widget.size, height: widget.size);
    }
    // Tint each half:
    // - When controlling: use the wisp's per-surface paint colour. Fall
    //   back to soft glow if a surface's colour hasn't landed yet
    //   (rare — firmware caches on first wisp paint).
    // - When in escape-hatch mode (not controlling, just advertising
    //   presence to advanced operators): both orbs use the wisp's
    //   offColor — the wisp isn't painting per-surface, it just is.
    final Color baseColor;
    final Color shadeColor;
    if (controlling) {
      baseColor = status.baseWispColor?.toSwatch() ??
          Colors.amber.withValues(alpha: 0.6);
      shadeColor = status.shadeWispColor?.toSwatch() ??
          Colors.lightBlueAccent.withValues(alpha: 0.6);
    } else {
      final off = status.offColor.toSwatch();
      baseColor = off;
      shadeColor = off;
    }
    return GestureDetector(
      onTap: _tap.record,
      behavior: HitTestBehavior.opaque,
      child: AnimatedBuilder(
        animation: Listenable.merge([_drift, _pulse]),
        builder: (context, _) {
          // When in escape-hatch mode the wisp isn't painting any
          // surface — mark both orbs inactive so the painter dims them
          // to ~35% brightness. Reads as "wisp is here but quiet."
          return CustomPaint(
            size: Size.square(widget.size),
            painter: _WispPainter(
              baseColor: baseColor,
              shadeColor: shadeColor,
              baseActive: controlling && status.controllingBase,
              shadeActive: controlling && status.controllingShade,
              drift: _drift.value,
              pulse: _pulse.value,
            ),
          );
        },
      ),
    );
  }
}

class _WispPainter extends CustomPainter {
  _WispPainter({
    required this.baseColor,
    required this.shadeColor,
    required this.baseActive,
    required this.shadeActive,
    required this.drift,
    required this.pulse,
  });

  final Color baseColor;
  final Color shadeColor;
  final bool baseActive;
  final bool shadeActive;

  /// 0..1 looping; drives the gentle figure-8 drift.
  final double drift;

  /// 0..1 ping-pong; drives the brightness pulse.
  final double pulse;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    final orbRadius = size.width * 0.20;

    // Two orbs drift on a small figure-8. drift = angle/2π.
    final theta = drift * 2 * math.pi;
    final ox = math.sin(theta) * size.width * 0.13;
    final oy = math.sin(theta * 2) * size.height * 0.07;

    // Pulse 0..1 → brightness multiplier 0.7..1.0
    final brightness = 0.7 + 0.3 * pulse;

    // Shade orb sits slightly higher (atop the lamp's shade in the
    // metaphor); base orb sits slightly lower.
    _drawOrb(
      canvas: canvas,
      center: Offset(cx + ox, cy - orbRadius * 0.9 - oy * 0.5),
      radius: orbRadius,
      color: shadeColor,
      brightness: shadeActive ? brightness : brightness * 0.35,
    );
    _drawOrb(
      canvas: canvas,
      center: Offset(cx - ox * 0.7, cy + orbRadius * 0.9 + oy * 0.5),
      radius: orbRadius,
      color: baseColor,
      brightness: baseActive ? brightness : brightness * 0.35,
    );
  }

  void _drawOrb({
    required Canvas canvas,
    required Offset center,
    required double radius,
    required Color color,
    required double brightness,
  }) {
    // Outer halo: soft radial gradient out to 2× radius. The halo's
    // alpha is what reads as "glow".
    final haloRadius = radius * 2.4;
    final haloPaint = Paint()
      ..shader = RadialGradient(
        colors: [
          color.withValues(alpha: 0.55 * brightness),
          color.withValues(alpha: 0.25 * brightness),
          color.withValues(alpha: 0.0),
        ],
        stops: const [0.0, 0.45, 1.0],
      ).createShader(Rect.fromCircle(center: center, radius: haloRadius));
    canvas.drawCircle(center, haloRadius, haloPaint);

    // Inner orb: solid-ish, brightness multiplier scales the alpha.
    final corePaint = Paint()
      ..shader = RadialGradient(
        colors: [
          Color.lerp(Colors.white, color, 0.4)!
              .withValues(alpha: 0.95 * brightness),
          color.withValues(alpha: 0.85 * brightness),
        ],
        stops: const [0.0, 1.0],
      ).createShader(Rect.fromCircle(center: center, radius: radius));
    canvas.drawCircle(center, radius, corePaint);
  }

  @override
  bool shouldRepaint(_WispPainter old) {
    return old.baseColor != baseColor ||
        old.shadeColor != shadeColor ||
        old.baseActive != baseActive ||
        old.shadeActive != shadeActive ||
        old.drift != drift ||
        old.pulse != pulse;
  }
}
