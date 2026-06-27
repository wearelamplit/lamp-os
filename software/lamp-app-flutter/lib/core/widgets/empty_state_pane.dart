import 'package:flutter/material.dart';

/// Shared visual pattern for "there's nothing here yet" / "we can't see X"
/// states. Used by the wisp tab's "No wisp detected" empty state and the
/// expressions tab's "No expressions yet" empty state, so the two read
/// as one design language rather than diverging snowflakes.
///
/// Pattern: centered icon + bold title + muted subtitle, with consistent
/// spacing and padding. Pass `icon` as a `Widget` so callers can use a
/// material `Icon`, a custom CustomPainter (the wisp's two-orb glyph),
/// or anything else.
class EmptyStatePane extends StatelessWidget {
  const EmptyStatePane({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
  });

  final Widget icon;
  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Center(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 32, vertical: 24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            icon,
            const SizedBox(height: 12),
            Text(
              title,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: colorScheme.onSurface,
                fontSize: 16,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              subtitle,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: colorScheme.onSurfaceVariant,
                fontSize: 12,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
