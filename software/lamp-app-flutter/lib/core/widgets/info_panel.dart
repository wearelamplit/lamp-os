import 'package:flutter/material.dart';

import '../theme/app_spacing.dart';

/// Small inline callout used to surface contextual help / explanations
/// inside settings forms. Ported from the Vue app's `InfoPanel.vue`:
/// linear gradient on aurora-blue, left border accent, fog-grey body text.
///
/// No horizontal margin: callers place it inside an already-padded list, so
/// it inherits the parent inset and stays flush with sibling cards/content.
class InfoPanel extends StatelessWidget {
  const InfoPanel({super.key, required this.child});
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.symmetric(vertical: AppSpace.sm),
      padding: const EdgeInsets.symmetric(
          horizontal: AppSpace.md, vertical: AppSpace.md),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            colorScheme.tertiary.withValues(alpha: 0.12),
            colorScheme.tertiary.withValues(alpha: 0.06),
          ],
        ),
        border: Border(
          left: BorderSide(color: colorScheme.tertiary, width: 3),
        ),
        borderRadius: const BorderRadius.only(
          topLeft: Radius.circular(2),
          topRight: Radius.circular(6),
          bottomRight: Radius.circular(6),
          bottomLeft: Radius.circular(2),
        ),
      ),
      child: DefaultTextStyle.merge(
        style: TextStyle(
          color: colorScheme.onSurfaceVariant,
          fontSize: 12,
          height: 1.5,
          letterSpacing: 0.1,
        ),
        child: child,
      ),
    );
  }
}
