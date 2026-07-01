import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../domain/wisp_source_mode.dart';

class SourcePicker extends StatelessWidget {
  const SourcePicker({
    super.key,
    required this.current,
    required this.auroraEnabled,
    required this.onSelect,
  });

  final WispSourceMode current;
  final bool auroraEnabled;
  final ValueChanged<WispSourceMode> onSelect;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Palette source'),
        const SizedBox(height: AppSpace.sm),
        Row(
          children: [
            _SourcePill(
              label: 'Off',
              icon: Icons.power_settings_new,
              selected: current == WispSourceMode.off,
              enabled: true,
              onTap: () => onSelect(WispSourceMode.off),
            ),
            const SizedBox(width: AppSpace.sm),
            _SourcePill(
              label: 'Manual',
              icon: Icons.palette_outlined,
              selected: current == WispSourceMode.manual,
              enabled: true,
              onTap: () => onSelect(WispSourceMode.manual),
            ),
            const SizedBox(width: AppSpace.sm),
            _SourcePill(
              label: 'Aurora',
              icon: Icons.auto_awesome,
              selected: current == WispSourceMode.aurora,
              enabled: auroraEnabled,
              onTap: () => onSelect(WispSourceMode.aurora),
            ),
          ],
        ),
        if (!auroraEnabled) ...[
          const SizedBox(height: 6),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 2),
            child: Text(
              "No Aurora zone heard yet. Once a zone shows up on the "
              "mesh, you'll be able to follow it.",
              style: Theme.of(context).textTheme.bodySmall?.copyWith(
                fontStyle: FontStyle.italic,
              ),
            ),
          ),
        ],
      ],
    );
  }
}

class _SourcePill extends StatelessWidget {
  const _SourcePill({
    required this.label,
    required this.icon,
    required this.selected,
    required this.enabled,
    required this.onTap,
  });

  final String label;
  final IconData icon;
  final bool selected;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    // selected → solid primary fill; idle → outline border.
    // Disabled drops opacity to signal "unavailable" without looking broken.
    final colorScheme = Theme.of(context).colorScheme;
    final Color fill = enabled && selected
        ? colorScheme.primary
        : Colors.transparent;
    final Color border = enabled && selected
        ? colorScheme.primary
        : colorScheme.outline.withValues(alpha: enabled ? 1.0 : 0.4);
    final Color fg = enabled && selected
        ? colorScheme.onPrimary
        : colorScheme.onSurface.withValues(alpha: enabled ? 1.0 : 0.4);

    return Expanded(
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: enabled ? onTap : null,
          borderRadius: BorderRadius.circular(AppRadius.card),
          child: Container(
            height: 72,
            decoration: BoxDecoration(
              color: fill,
              border: Border.all(
                color: border,
                width: selected && enabled ? 2 : 1,
              ),
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, color: fg, size: 22),
                const SizedBox(height: 4),
                Text(
                  label,
                  style: TextStyle(
                    color: fg,
                    fontSize: 13,
                    fontWeight: selected && enabled
                        ? FontWeight.w700
                        : FontWeight.w500,
                    letterSpacing: 0.5,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
