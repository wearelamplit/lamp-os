import 'package:flutter/material.dart';

import '../theme/app_spacing.dart';
import '../theme/brand_extras.dart';

/// Bottom [NavigationBar] with the brand chrome-gradient active indicator.
/// Material 3 only allows a flat indicator color, so the selected
/// destination renders the gradient pill via a custom icon decoration.
class GradientNavBar extends StatelessWidget {
  const GradientNavBar({
    super.key,
    required this.selectedIndex,
    required this.onDestinationSelected,
    required this.destinations,
  });

  final int selectedIndex;
  final ValueChanged<int> onDestinationSelected;
  final List<({IconData icon, String label})> destinations;

  NavigationDestination _destination(
      BuildContext context, IconData icon, String label, bool selected) {
    final iconWidget = Icon(icon, size: 22);
    if (!selected) return NavigationDestination(icon: iconWidget, label: label);
    final gradient = context.brandExtras.chromeGradient;
    final primary = Theme.of(context).colorScheme.primary;
    return NavigationDestination(
      icon: Container(
        padding: const EdgeInsets.symmetric(
            horizontal: AppSpace.lg, vertical: AppSpace.xs),
        decoration: BoxDecoration(
          gradient: gradient,
          borderRadius: BorderRadius.circular(999), // pill shape, not spacing
          boxShadow: [
            BoxShadow(
              color: primary.withValues(alpha: 0.3),
              blurRadius: 8,
              offset: const Offset(0, 2),
            ),
          ],
        ),
        child: iconWidget,
      ),
      label: label,
    );
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return NavigationBarTheme(
      data: NavigationBarThemeData(
        indicatorColor: Colors.transparent,
        indicatorShape: const StadiumBorder(),
        labelTextStyle: WidgetStateProperty.resolveWith((states) {
          if (states.contains(WidgetState.selected)) {
            return TextStyle(
              color: cs.onSurface,
              fontWeight: FontWeight.w600,
              fontSize: 12,
            );
          }
          return TextStyle(color: cs.onSurfaceVariant, fontSize: 12);
        }),
        iconTheme: WidgetStateProperty.resolveWith((states) {
          return IconThemeData(
            color: states.contains(WidgetState.selected)
                ? cs.onSurface
                : cs.onSurfaceVariant,
          );
        }),
      ),
      child: NavigationBar(
        selectedIndex: selectedIndex,
        onDestinationSelected: onDestinationSelected,
        destinations: [
          for (var i = 0; i < destinations.length; i++)
            _destination(context, destinations[i].icon, destinations[i].label,
                i == selectedIndex),
        ],
      ),
    );
  }
}
