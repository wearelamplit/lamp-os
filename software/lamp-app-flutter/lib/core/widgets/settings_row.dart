import 'package:flutter/material.dart';
import '../theme/app_spacing.dart';

/// Mobile-style settings row: leading icon, title + optional subtitle,
/// optional trailing widget (toggle / chevron). Used everywhere on the
/// Setup tab to give it the iOS/Android-Settings look from the old Vue
/// app instead of inline form cards.
class SettingsRow extends StatelessWidget {
  const SettingsRow({
    super.key,
    required this.icon,
    required this.title,
    this.subtitle,
    this.trailing,
    this.onTap,
    this.drillChevron = false,
  });

  final IconData icon;
  final String title;
  final String? subtitle;
  final Widget? trailing;
  final VoidCallback? onTap;
  final bool drillChevron;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.lg, vertical: AppSpace.lg),
          decoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(
                color: colorScheme.onSurface.withValues(alpha: 0.06),
                width: 1, // deliberate dimension, not spacing
              ),
            ),
          ),
          child: Row(
            children: [
              Container(
                width: 36, // deliberate dimension, not spacing
                height: 36, // deliberate dimension, not spacing
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: colorScheme.primaryContainer.withValues(alpha: 0.16),
                ),
                child: Icon(icon, size: 18, color: colorScheme.onPrimaryContainer), // deliberate dimension, not spacing
              ),
              const SizedBox(width: AppSpace.md),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(title, style: textTheme.titleMedium),
                    if (subtitle != null) ...[
                      const SizedBox(height: 2), // deliberate dimension, not spacing
                      Text(subtitle!, style: textTheme.bodySmall),
                    ],
                  ],
                ),
              ),
              if (trailing != null) ...[
                const SizedBox(width: AppSpace.sm),
                trailing!,
              ],
              if (onTap != null && (trailing == null || drillChevron)) ...[
                if (trailing != null) const SizedBox(width: AppSpace.xs),
                Icon(Icons.chevron_right,
                    color: colorScheme.onSurfaceVariant, size: 22), // deliberate dimension, not spacing
              ],
            ],
          ),
        ),
      ),
    );
  }
}

/// Short label heading used between row groups to separate them.
class SettingsGroupHeading extends StatelessWidget {
  const SettingsGroupHeading(this.text, {super.key});
  final String text;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return Padding(
      padding: const EdgeInsets.fromLTRB(AppSpace.lg, AppSpace.xl, AppSpace.lg, AppSpace.sm),
      child: Text(
        text.toUpperCase(),
        style: textTheme.labelLarge?.copyWith(
          color: colorScheme.secondary,
          fontWeight: FontWeight.w700,
        ),
      ),
    );
  }
}
