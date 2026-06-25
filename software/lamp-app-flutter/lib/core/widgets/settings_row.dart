import 'package:flutter/material.dart';

import '../theme/brand_colors.dart';

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
  });

  final IconData icon;
  final String title;
  final String? subtitle;
  final Widget? trailing;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
          decoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(
                color: BrandColors.lampWhite.withValues(alpha: 0.06),
                width: 1,
              ),
            ),
          ),
          child: Row(
            children: [
              Container(
                width: 36,
                height: 36,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: BrandColors.auroraBlue.withValues(alpha: 0.16),
                ),
                child: Icon(icon, size: 18, color: BrandColors.auroraBlue),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        color: BrandColors.lampWhite,
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    if (subtitle != null) ...[
                      const SizedBox(height: 2),
                      Text(
                        subtitle!,
                        style: const TextStyle(
                          color: BrandColors.fogGrey,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              if (trailing != null) ...[
                const SizedBox(width: 8),
                trailing!,
              ],
              if (onTap != null && trailing == null)
                const Icon(Icons.chevron_right,
                    color: BrandColors.slateGrey, size: 22),
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
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 24, 16, 8),
      child: Text(
        text.toUpperCase(),
        style: const TextStyle(
          color: BrandColors.headerYellow,
          fontSize: 11,
          fontWeight: FontWeight.w700,
          letterSpacing: 1.2,
        ),
      ),
    );
  }
}
