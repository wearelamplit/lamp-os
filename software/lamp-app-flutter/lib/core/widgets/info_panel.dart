import 'package:flutter/material.dart';

/// Small inline callout used to surface contextual help / explanations
/// inside settings forms. Ported from the Vue app's `InfoPanel.vue`:
/// linear gradient on aurora-blue, left border accent, fog-grey body text.
class InfoPanel extends StatelessWidget {
  const InfoPanel({super.key, required this.child});
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Color(0x1F446C9C), // auroraBlue at ~12% alpha
            Color(0x0F446C9C), // auroraBlue at ~6% alpha
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
