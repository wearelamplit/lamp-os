import 'dart:async';

import 'package:flutter/material.dart';
import 'package:lamp_app/core/utils/string_case.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../../core/routing/routes.dart';
import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/status_dot.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../inventory/domain/lamp_colors.dart';
import '../../../inventory/presentation/lamp_grid_tile.dart';
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
    return GridView(
      padding: const EdgeInsets.fromLTRB(
          AppSpace.lg, AppSpace.md, AppSpace.lg, AppSpace.xl),
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
        maxCrossAxisExtent: 200, // deliberate dimension, not spacing
        mainAxisSpacing: AppSpace.md,
        crossAxisSpacing: AppSpace.md,
        childAspectRatio: 0.88,
      ),
      children: [
        for (final lamp in lamps) _LampTile(lamp: lamp),
      ],
    );
  }
}

class _LampTile extends ConsumerWidget {
  const _LampTile({required this.lamp});
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
    // BLE adv reports this lamp is bluetooth-reachable; `isMesh`
    // distinguishes mesh-protocol firmware (green dot) from legacy
    // BT-only (blue dot).
    final status = lamp.isMesh ? StatusKind.mesh : StatusKind.bluetooth;
    return LampGridTile(
      deviceId: lamp.id,
      colors: resolveLampColors(near: lamp),
      status: status,
      name: lamp.name.isEmpty ? '(unnamed)' : lamp.name.toTitleCase(),
      rssi: lamp.rssi,
      onTap: () => _onTap(context, ref),
    );
  }
}
