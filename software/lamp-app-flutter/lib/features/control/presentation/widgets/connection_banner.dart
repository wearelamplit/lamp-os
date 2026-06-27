import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';

/// Small amber banner shown at the top of the Control screen when the BLE
/// link to the lamp has dropped. Local edits keep working — the writes just
/// silently queue until the link is back. ControlNotifier owns the retry
/// loop; this widget is purely informational.
class ConnectionBanner extends StatelessWidget {
  const ConnectionBanner({super.key, required this.attempt});

  /// 1 for the first try, 2 for the second, etc.
  final int attempt;

  @override
  Widget build(BuildContext context) {
    final warn = Theme.of(context).colorScheme.secondary;
    return Semantics(
      liveRegion: true,
      label: 'Reconnecting to lamp',
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(
            horizontal: AppSpace.lg, vertical: AppSpace.sm),
        color: warn.withValues(alpha: 0.18),
        child: Row(
          children: [
            SizedBox(
              width: 14,
              height: 14,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: warn,
              ),
            ),
            const SizedBox(width: AppSpace.md),
            Expanded(
              child: Text(
                attempt <= 1
                    ? 'Lamp dropped out — reconnecting…'
                    : 'Reconnecting (attempt $attempt)…',
                style: Theme.of(context)
                    .textTheme
                    .bodySmall
                    ?.copyWith(color: warn),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
