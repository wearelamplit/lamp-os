import 'package:flutter/material.dart';

import '../../../../core/theme/brand_colors.dart';

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
    return Semantics(
      liveRegion: true,
      label: 'Reconnecting to lamp',
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        color: BrandColors.amberGold.withValues(alpha: 0.18),
        child: Row(
          children: [
            const SizedBox(
              width: 14,
              height: 14,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: BrandColors.amberGold,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                attempt <= 1
                    ? 'Lamp dropped out — reconnecting…'
                    : 'Reconnecting (attempt $attempt)…',
                style: const TextStyle(
                  color: BrandColors.amberGold,
                  fontSize: 12,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
