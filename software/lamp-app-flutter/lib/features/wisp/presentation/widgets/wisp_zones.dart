import 'package:flutter/material.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/section_header.dart';
import '../../domain/wisp_status.dart';
import '../../domain/zone_source.dart';

/// Inline notice shown under the Aurora source pill when Aurora is
/// selected but the wisp hasn't reached Aurora's network yet.
class AuroraNotConnectedNotice extends StatelessWidget {
  const AuroraNotConnectedNotice({super.key, required this.wifiConnected});
  final bool wifiConnected;

  @override
  Widget build(BuildContext context) {
    final detail = wifiConnected
        ? "Wi-Fi is up but Aurora hasn't been reached yet."
        : "The wisp isn't on Wi-Fi — configure it below.";
    final secondary = Theme.of(context).colorScheme.secondary;
    return Container(
      padding: const EdgeInsets.all(AppSpace.md),
      decoration: BoxDecoration(
        color: secondary.withValues(alpha: 0.10),
        border: Border.all(
          color: secondary.withValues(alpha: 0.40),
        ),
        borderRadius: BorderRadius.circular(AppSpace.sm),
      ),
      child: Row(
        children: [
          Icon(
            Icons.auto_awesome,
            size: 16,
            color: secondary,
          ),
          const SizedBox(width: AppSpace.sm),
          Expanded(
            child: Text(
              'Aurora not connected.\n$detail',
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
        ],
      ),
    );
  }
}

class CurrentZone extends StatelessWidget {
  const CurrentZone({super.key, required this.status});
  final WispStatus status;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    final String headline;
    final String? subhead;
    // The wisp publishes a -1 sentinel for "no current zone heard yet"
    // (firmware-side equivalent of null when ZoneSelector hasn't latched).
    // Show that as a human-readable "None detected" instead of the
    // literal "Zone -1" the operator was seeing.
    final zone = status.currentZone;
    final zoneHeard = zone != null && zone >= 0;
    if (!zoneHeard) {
      headline = 'None detected';
      subhead = status.zoneSource == ZoneSource.none
          ? 'Tap a zone below to assign one — or wait for Aurora to '
              'publish one.'
          : null;
    } else {
      headline = 'Zone $zone';
      subhead = _zoneSourceLabel(status.zoneSource);
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Current zone'),
        const SizedBox(height: AppSpace.sm),
        Text(
          headline,
          style: textTheme.displaySmall,
        ),
        if (subhead != null) ...[
          const SizedBox(height: 4),
          Text(
            subhead,
            style: textTheme.bodySmall?.copyWith(fontStyle: FontStyle.italic),
          ),
        ],
      ],
    );
  }

  /// Maps the `zoneSource` enum to the parenthetical "where did this
  /// come from?" sub-label on the current-zone card.
  static String _zoneSourceLabel(ZoneSource source) {
    switch (source) {
      case ZoneSource.appOp:
        return 'Set in app';
      case ZoneSource.nvs:
        return 'Persisted on the wisp';
      case ZoneSource.firstSeen:
        return 'First zone heard on the mesh';
      case ZoneSource.none:
      case ZoneSource.unknown:
        return '';
    }
  }
}

class ObservedZonesPicker extends StatelessWidget {
  const ObservedZonesPicker({
    super.key,
    required this.status,
    required this.onPickZone,
  });
  final WispStatus status;
  final ValueChanged<int> onPickZone;

  @override
  Widget build(BuildContext context) {
    final zones = [...status.observedZones]..sort();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionHeader('Observed zones'),
        const SizedBox(height: AppSpace.sm),
        if (zones.isEmpty)
          Text(
            "No zones heard yet. Once Aurora starts publishing zone "
            "palettes on the mesh, they'll appear here.",
            style: Theme.of(context).textTheme.bodySmall,
          )
        else
          Wrap(
            spacing: AppSpace.sm,
            runSpacing: AppSpace.sm,
            children: [
              for (final z in zones)
                _ZoneChip(
                  zoneId: z,
                  selected: z == status.currentZone,
                  onTap: () => onPickZone(z),
                ),
            ],
          ),
      ],
    );
  }
}

class _ZoneChip extends StatelessWidget {
  const _ZoneChip({
    required this.zoneId,
    required this.selected,
    required this.onTap,
  });
  final int zoneId;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final fill = selected ? colorScheme.primary : Colors.transparent;
    final border = selected ? colorScheme.primary : colorScheme.outline;
    final fg = selected ? colorScheme.onPrimary : colorScheme.onSurface;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        borderRadius: BorderRadius.circular(999),
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.lg, vertical: 10),
          decoration: BoxDecoration(
            color: fill,
            borderRadius: BorderRadius.circular(999),
            border: Border.all(color: border, width: selected ? 2 : 1),
          ),
          child: Text(
            'Zone $zoneId',
            style: TextStyle(
              color: fg,
              fontSize: 14,
              fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
            ),
          ),
        ),
      ),
    );
  }
}
