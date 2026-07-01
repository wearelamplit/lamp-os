import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../domain/wisp_status.dart';

class WispHeader extends StatelessWidget {
  const WispHeader({super.key, required this.status, required this.staleSeconds});
  final WispStatus status;
  final int? staleSeconds;

  static const _staleThresholdSec = 60;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    if (!status.present) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: AppSpace.lg),
        child: Text(
          'No wisp detected.',
          style: textTheme.bodyMedium,
        ),
      );
    }
    final mac = status.wispMac ?? '';
    final stale = staleSeconds != null && staleSeconds! > _staleThresholdSec;
    final freshnessLabel = staleSeconds == null
        ? 'Just connected'
        : 'Last seen ${staleSeconds}s ago';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(
              Icons.bubble_chart,
              color: Theme.of(context).colorScheme.tertiary,
              size: 22,
            ),
            const SizedBox(width: AppSpace.sm),
            Expanded(
              child: Text(
                mac,
                overflow: TextOverflow.ellipsis,
                style: textTheme.titleMedium?.copyWith(
                  fontSize: 16,
                  letterSpacing: 0.5,
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Text(
          freshnessLabel,
          style: textTheme.bodySmall?.copyWith(
            color: stale ? colorScheme.error : colorScheme.onSurfaceVariant,
            fontStyle: stale ? FontStyle.italic : FontStyle.normal,
          ),
        ),
      ],
    );
  }
}
