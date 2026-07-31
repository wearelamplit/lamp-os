import 'package:flutter/material.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/critter_icon.dart';
import '../../../core/widgets/status_dot.dart';
import '../domain/lamp_colors.dart';
import '../domain/signal_level.dart';

/// Critter tile shared by the My Lamps picker and the Adopt scan list, so both
/// grids read identically. Callers own the data; this renders the visual: a
/// 64px critter with a corner [StatusDot] badge and a centered name.
/// [highlighted] draws the active-lamp border; an [StatusKind.offline] status
/// dims the critter and greys the name. [rssi] grades the mesh dot's green
/// brightness by signal strength (null = medium).
class LampGridTile extends StatelessWidget {
  const LampGridTile({
    super.key,
    required this.deviceId,
    required this.colors,
    required this.status,
    required this.name,
    this.rssi,
    this.highlighted = false,
    this.subtitle,
    this.onTap,
    this.onLongPress,
  });

  final String deviceId;
  final LampColors colors;
  final StatusKind status;
  final String name;
  final int? rssi;
  final bool highlighted;
  /// Optional secondary line under the name, rendered in the busy accent.
  /// Used for the "teaching new tricks" note on an OTA-distributing lamp.
  final String? subtitle;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    final offline = status == StatusKind.offline;
    return InkWell(
      borderRadius: BorderRadius.circular(AppRadius.card),
      onTap: onTap,
      onLongPress: onLongPress,
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.all(AppSpace.md),
        decoration: BoxDecoration(
          color: colorScheme.surfaceContainerHighest
              .withValues(alpha: highlighted ? 1.0 : 0.4),
          borderRadius: BorderRadius.circular(AppRadius.card),
          border: highlighted
              ? Border.all(
                  color: colorScheme.primary,
                  width: 2, // deliberate dimension, not spacing
                )
              : null,
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Stack(
              clipBehavior: Clip.none,
              alignment: Alignment.center,
              children: [
                Opacity(
                  opacity: offline ? 0.5 : 1.0,
                  child: CritterIcon(
                    deviceId: deviceId,
                    shade: colors.shade ?? colorScheme.onSurfaceVariant,
                    base: colors.base ?? colorScheme.onSurfaceVariant,
                    size: 64, // deliberate dimension, not spacing
                  ),
                ),
                Positioned(
                  right: -2, // deliberate dimension, not spacing
                  top: -2,
                  child: StatusDot(
                    kind: status,
                    size: 14, // deliberate dimension, not spacing
                    brightness: signalDotBrightnessFor(rssi),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AppSpace.sm),
            Text(
              name,
              textAlign: TextAlign.center,
              maxLines: subtitle == null ? 2 : 1,
              overflow: TextOverflow.ellipsis,
              style: textTheme.titleSmall?.copyWith(
                color: offline ? colorScheme.onSurfaceVariant : null,
                fontWeight: highlighted ? FontWeight.w700 : null,
              ),
            ),
            if (subtitle != null) ...[
              const SizedBox(height: AppSpace.xs),
              Text(
                subtitle!,
                textAlign: TextAlign.center,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: textTheme.bodySmall?.copyWith(
                  color: colorScheme.secondary,
                  fontSize: 11, // deliberate dimension, not spacing
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
