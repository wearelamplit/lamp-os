import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../inventory/domain/inventory_lamp.dart';

/// Lamp firmware from which the lamp serves the `wispconfig` section, packed
/// as `(major << 16) | (minor << 8) | patch`. Through an older lamp the
/// wisp's settings never arrive.
const int kWispConfigMinLampFw = 0x010208;

/// Placeholder for a settings control while the wisp's real settings have
/// not arrived ([WispStatus.configKnown] false). Stands in for the control
/// so nothing a save could write back is ever shown as a default. Through a
/// lamp at [lampId] older than [kWispConfigMinLampFw] it asks for a lamp
/// update instead of waiting.
class WispSettingsPending extends ConsumerWidget {
  const WispSettingsPending({super.key, required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final fw = ref.watch(
      inventoryNotifierProvider.select<int?>((inv) {
        for (final l in inv.value ?? const <InventoryLamp>[]) {
          if (l.id == lampId) return l.fwVersion;
        }
        return null;
      }),
    );
    final style = Theme.of(
      context,
    ).textTheme.bodySmall?.copyWith(fontStyle: FontStyle.italic);
    if (fw != null && fw < kWispConfigMinLampFw) {
      return Padding(
        key: const Key('wisp-settings-pending'),
        padding: const EdgeInsets.symmetric(vertical: AppSpace.md),
        child: Text("Update this lamp to see the wisp's settings", style: style),
      );
    }
    return Padding(
      key: const Key('wisp-settings-pending'),
      padding: const EdgeInsets.symmetric(vertical: AppSpace.md),
      child: Row(
        children: [
          const SizedBox(
            width: 14,
            height: 14, // deliberate dimension, not spacing
            child: CircularProgressIndicator(strokeWidth: 2),
          ),
          const SizedBox(width: AppSpace.md),
          Text('Reading settings from wisp…', style: style),
        ],
      ),
    );
  }
}
