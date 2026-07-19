import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../../core/routing/routes.dart';
import '../../../../core/theme/app_spacing.dart';
import '../../../../core/theme/brand_extras.dart';
import '../../../../core/widgets/critter_icon.dart';
import '../../../../core/widgets/lamp_card.dart';
import '../../../../core/widgets/status_dot.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../inventory/domain/lamp_colors.dart';
import '../../../nearby/application/nearby_lamps_notifier.dart';
import '../../../nearby/domain/nearby_lamp.dart';
import '../../application/add_lamp_notifier.dart';

class AddLampScanStep extends ConsumerWidget {
  const AddLampScanStep({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final all = ref.watch(nearbyLampsNotifierProvider);
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const [];
    final inventoryIds = inventory.map((l) => l.id).toSet();
    // Hide lamps that are already in this phone's inventory: they're
    // already addable from "My lamps", and showing them here would
    // confuse the "tap a discovered lamp to add it" flow.
    final lamps = all.where((l) => !inventoryIds.contains(l.id)).toList();
    if (lamps.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.xxl),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                'Searching for a stray lamp…',
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.titleMedium,
              ),
              const SizedBox(height: AppSpace.sm),
              Text(
                'Make sure your new lamp is plugged in and glowing nearby. '
                "If they're shy, give them a moment.",
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium,
              ),
            ],
          ),
        ),
      );
    }
    return ListView.separated(
      padding: const EdgeInsets.all(AppSpace.lg),
      itemCount: lamps.length,
      separatorBuilder: (_, index) => const SizedBox(height: AppSpace.sm),
      itemBuilder: (context, i) => _LampRow(lamp: lamps[i]),
    );
  }
}

class _LampRow extends ConsumerWidget {
  const _LampRow({required this.lamp});
  final NearbyLamp lamp;

  Future<void> _onTap(BuildContext context, WidgetRef ref) async {
    // Legacy (non-mesh) firmware can't be controlled by the app: there's
    // no GATT control service. Route to the BtOnly explanation screen
    // instead of adopting; that page tells the user how to use the
    // lamp's own Wi-Fi AP and how to flash current firmware. Crucially
    // this does NOT add the lamp to inventory: adopting an unreachable
    // lamp would just leave a dead tile in the picker.
    if (!lamp.isMesh) {
      // Fire-and-forget: not awaiting the routed screen's
      // pop, just sending the user there.
      unawaited(GoRouter.maybeOf(context)?.push(AppRoutes.btOnly(lamp.id))
          ?? Future<void>.value());
      return;
    }
    if (lamp.isFactoryDefault) {
      // select() is synchronous: it records the deviceId and advances
      // to Name without opening a BLE link. The link is opened in
      // submit() so it doesn't sit idle through the form-fill and
      // expire under LINK_SUPERVISION_TIMEOUT.
      ref.read(addLampNotifierProvider.notifier).select(
            lamp.id,
            baseRgb: lamp.baseRgb,
            shadeRgb: lamp.shadeRgb,
          );
    } else {
      // No confirm dialog: `add()` sets state.step to `done` and the
      // AddLampShell will swap in the AddLampDoneStep ("X is home!"),
      // which serves as the visual confirmation.
      await ref
          .read(addLampNotifierProvider.notifier)
          .add(deviceId: lamp.id, name: lamp.name);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final colorScheme = Theme.of(context).colorScheme;
    final colors = resolveLampColors(near: lamp);
    return InkWell(
      borderRadius: BorderRadius.circular(AppRadius.card),
      onTap: () => _onTap(context, ref),
      child: LampCard(
        padding: const EdgeInsets.all(AppSpace.md),
        child: Row(
          children: [
            // BLE adv reports this lamp is bluetooth-reachable; the
            // `isMesh` flag distinguishes mesh-protocol firmware from
            // legacy BT-only. Light green for mesh, faded blue for BT.
            StatusDot(
              kind: lamp.isMesh
                  ? StatusKind.mesh
                  : StatusKind.bluetooth,
              size: 14,
            ),
            const SizedBox(width: AppSpace.md),
            CritterIcon(
              deviceId: lamp.id,
              shade: colors.shade ?? colorScheme.onSurfaceVariant,
              base: colors.base ?? colorScheme.onSurfaceVariant,
              size: 44,
            ),
            const SizedBox(width: AppSpace.md),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    lamp.name.isEmpty ? '(unnamed)' : lamp.name,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                  Text(
                    '${lamp.id} · ${lamp.rssi} dBm',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                      fontSize: 11,
                    ),
                  ),
                ],
              ),
            ),
            _Pill(
              factoryDefault: lamp.isFactoryDefault,
              isMesh: lamp.isMesh,
            ),
          ],
        ),
      ),
    );
  }
}

class _Pill extends StatelessWidget {
  const _Pill({required this.factoryDefault, required this.isMesh});
  final bool factoryDefault;
  final bool isMesh;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final extras = context.brandExtras;
    final Color base;
    final String label;
    if (!isMesh) {
      base = colorScheme.onSurfaceVariant;
      label = 'legacy';
    } else if (factoryDefault) {
      base = colorScheme.secondary;
      label = 'adopt';
    } else {
      base = extras.success;
      label = 'add';
    }
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: AppSpace.sm, vertical: AppSpace.xs),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(999), // pill shape, not spacing
        color: base.withValues(alpha: 0.18),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 10,
          color: base,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}
