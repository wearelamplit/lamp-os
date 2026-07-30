import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/theme/app_spacing.dart';
import '../../control/application/control_notifier.dart';
import '../../firmware/application/firmware_notifier.dart';
import '../../firmware/domain/firmware_state.dart';
import '../../inventory/application/active_lamp_notifier.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../inventory/domain/inventory_lamp.dart';
import '../../inventory/domain/lamp_colors.dart';
import '../../lamp_shell/application/lamp_status.dart';
import '../../nearby/application/lamp_route_resolver.dart';
import '../../nearby/application/nearby_lamps_notifier.dart';
import '../../nearby/application/scan_grace_provider.dart';
import '../../nearby/domain/nearby_lamp.dart';
import 'lamp_grid_tile.dart';

/// Unified lamp picker. The app's landing screen for users with at least
/// one lamp, and also the destination of LampShell's "switch lamp" action.
///
/// One full-screen widget, one ordered grid of critter tiles (no
/// online/offline section headers), a long-press action sheet per tile, and
/// an "Adopt a lamp" tile at the end. The active lamp is
/// pinned to the top; the rest hold a fixed alphabetical order (see
/// [sortMyLamps]) so tiles don't churn as lamps move in and out of range.
///
/// The scanner is mounted while this screen is alive (via the watch on
/// `nearbyLampsNotifierProvider`) and torn down when the user navigates
/// away. A scanning chip near the top signals the live feed.
class MyLampsScreen extends ConsumerWidget {
  const MyLampsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const [];
    final nearby = ref.watch(nearbyLampsNotifierProvider);
    final activeId = ref.watch(activeLampNotifierProvider).value;
    final inScanGrace = ref.watch(scanGraceActiveProvider);
    final nearbyById = <String, NearbyLamp>{for (final n in nearby) n.id: n};

    final ordered = sortMyLamps(inventory, activeId);

    return Scaffold(
      appBar: AppBar(
        title: const Text('My lamps'),
      ),
      body: SafeArea(
        child: GridView(
          padding: const EdgeInsets.fromLTRB(
              AppSpace.lg, AppSpace.md, AppSpace.lg, AppSpace.xl),
          gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
            maxCrossAxisExtent: 200, // deliberate dimension, not spacing
            mainAxisSpacing: AppSpace.md,
            crossAxisSpacing: AppSpace.md,
            childAspectRatio: 0.88,
          ),
          children: [
            for (final lamp in ordered)
              _LampTile(
                lamp: lamp,
                nearbyById: nearbyById,
                isCurrent: lamp.id == activeId,
                inScanGrace: inScanGrace,
              ),
            const _AddLampTile(),
          ],
        ),
      ),
    );
  }
}

/// The active lamp pins to the top; every other lamp orders alphabetically by
/// name (case-insensitive), with `id` as a stable tiebreak so equal names
/// don't shuffle. No RSSI / last-seen term, so a lamp moving in and out of
/// range never reorders the list.
List<InventoryLamp> sortMyLamps(List<InventoryLamp> inv, String? activeId) {
  final sorted = [...inv];
  sorted.sort((a, b) {
    final aActive = a.id == activeId;
    final bActive = b.id == activeId;
    if (aActive != bActive) return aActive ? -1 : 1;
    final byName = a.name.toLowerCase().compareTo(b.name.toLowerCase());
    if (byName != 0) return byName;
    return a.id.compareTo(b.id);
  });
  return sorted;
}


class _LampTile extends ConsumerWidget {
  const _LampTile({
    required this.lamp,
    required this.nearbyById,
    required this.isCurrent,
    required this.inScanGrace,
  });

  final InventoryLamp lamp;
  final Map<String, NearbyLamp> nearbyById;
  final bool isCurrent;
  final bool inScanGrace;

  Future<void> _onTap(BuildContext context, WidgetRef ref) async {
    // Tapping any tile (including the currently-active lamp) navigates to
    // its control screen. From the full-screen picker the user is not on the
    // lamp's screen, so "tap active" needs to take them there.
    //
    // `.go()` not `.push()` is load-bearing: push stacks LampShell instances
    // on top of each other (and each one keeps its controlNotifier alive,
    // which means N reconnect ladders chewing the BLE radio in parallel).
    // .go() collapses the stack to the destination so only the active lamp's
    // controlNotifier exists at any time.
    await ref.read(activeLampNotifierProvider.notifier).set(lamp.id);
    if (!context.mounted) return;
    final inv = ref.read(inventoryNotifierProvider).value;
    final nearby = nearbyById.values.toList(growable: false);
    GoRouter.maybeOf(context)?.go(
      routeForLamp(lamp.id, nearby, inventory: inv),
    );
  }

  Future<void> _showLampActions(BuildContext context, WidgetRef ref) async {
    final action = await showModalBottomSheet<_LampAction>(
      context: context,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.lock_reset),
              title: const Text('Reset connection password'),
              subtitle: const Text(
                'Use if Save / rename keeps reverting on this lamp.',
              ),
              onTap: () => Navigator.pop(ctx, _LampAction.resetPassword),
            ),
            ListTile(
              leading: Icon(Icons.delete_outline,
                  color: Theme.of(ctx).colorScheme.error),
              title: Text(
                'Remove',
                style: TextStyle(color: Theme.of(ctx).colorScheme.error),
              ),
              onTap: () => Navigator.pop(ctx, _LampAction.remove),
            ),
          ],
        ),
      ),
    );
    if (action == null || !context.mounted) return;
    switch (action) {
      case _LampAction.resetPassword:
        await ref
            .read(inventoryNotifierProvider.notifier)
            .updatePassword(lamp.id, null);
        ref.invalidate(controlNotifierProvider(lamp.id));
        if (!context.mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Cleared cached password for ${lamp.name}')),
        );
      case _LampAction.remove:
        if (!await _confirmRemoveDialog(context, lamp.name)) return;
        // If this was the active lamp, repoint activeLampNotifier so nothing
        // dangles at a deleted id.
        final activeBefore = ref.read(activeLampNotifierProvider).value;
        await ref.read(inventoryNotifierProvider.notifier).remove(lamp.id);
        ref.invalidate(controlNotifierProvider(lamp.id));
        if (lamp.id == activeBefore) {
          final remaining =
              ref.read(inventoryNotifierProvider).value ?? const [];
          if (remaining.isEmpty) {
            await ref.read(activeLampNotifierProvider.notifier).clear();
          } else {
            await ref
                .read(activeLampNotifierProvider.notifier)
                .set(remaining.first.id);
          }
        }
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Connected lamps don't advertise (NimBLE peripheral default), so the
    // BLE scan never sees them. Checking only `nearbyById` would render
    // them as "offline". Watch the lamp's live connection state so an
    // unsolicited link drop (powered-off lamp hitting supervision timeout)
    // repaints this tile immediately, instead of holding green until an
    // unrelated rebuild. Seed with the synchronous `isConnected` read so the
    // first paint is correct before the stream's initial emit lands.
    final bleClient = ref.watch(bleClientProvider);
    final connectedToThisLamp =
        ref.watch(lampConnectedProvider(lamp.id)).value ??
            bleClient.isConnected(lamp.id);
    // For the active lamp, also fold in the controlNotifier's `connected`
    // signal. fbp may report connected mid-handshake before the app has
    // a usable GATT session. The `.select` keeps slider drags from
    // rebuilding this tile.
    final notifierConnected = isCurrent &&
        ref.watch(controlNotifierProvider(lamp.id).select(
          (async) => async.value?.connected ?? false,
        ));
    // Keyed by BLE deviceId (== lamp.id), the firmwareNotifier family key.
    // `.select` so a Streaming chunk-progress tick doesn't rebuild the tile;
    // only a push-phase transition does.
    final updating = ref.watch(
      firmwareNotifierProvider(lamp.id).select((s) => s.isPushing),
    );
    final status = statusForById(
      lampId: lamp.id,
      nearbyById: nearbyById,
      connected: connectedToThisLamp || notifierConnected,
      updating: updating,
      inScanGrace: inScanGrace,
    );
    final hit = nearbyById[lamp.id];
    final colors = resolveLampColors(inv: lamp, near: hit);

    // Every tile is tappable, even offline ones. The user may want to
    // navigate to a lamp's screen to wait for its reconnect or see its
    // last-known state, even when not currently in range.
    return LampGridTile(
      deviceId: (lamp.lampId?.isNotEmpty ?? false) ? lamp.lampId! : lamp.id,
      colors: colors,
      status: status,
      name: lamp.name,
      rssi: hit?.rssi,
      highlighted: isCurrent,
      onTap: () => _onTap(context, ref),
      onLongPress: () => _showLampActions(context, ref),
    );
  }
}

/// Catch-all "Adopt a lamp" tile rendered after the inventory list.
class _AddLampTile extends StatelessWidget {
  const _AddLampTile();

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return InkWell(
      borderRadius: BorderRadius.circular(AppRadius.card),
      onTap: () => GoRouter.maybeOf(context)?.push(AppRoutes.addLamp),
      child: Container(
        padding: const EdgeInsets.all(AppSpace.md),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(AppRadius.card),
          border: Border.all(color: colorScheme.outlineVariant),
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Container(
              width: 64, // deliberate dimension, not spacing
              height: 64,
              decoration: BoxDecoration(
                color: colorScheme.surfaceContainerHighest,
                shape: BoxShape.circle,
              ),
              child: Icon(Icons.add, color: colorScheme.onSurface),
            ),
            const SizedBox(height: AppSpace.sm),
            Text(
              'Adopt a lamp',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.titleSmall,
            ),
          ],
        ),
      ),
    );
  }
}

/// Confirmation gate for the long-press remove action.
Future<bool> _confirmRemoveDialog(BuildContext context, String lampName) async {
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('Remove this lamp?'),
      content: Text(
        '$lampName will be removed from your lamps on this phone. '
        "The lamp itself keeps its name, password, and Wi-Fi. You can "
        'add it back later from the picker.',
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Cancel'),
        ),
        FilledButton(
          style: FilledButton.styleFrom(
              backgroundColor: Theme.of(ctx).colorScheme.error,
              foregroundColor: Theme.of(ctx).colorScheme.onError),
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Remove'),
        ),
      ],
    ),
  );
  return ok == true;
}

enum _LampAction { resetPassword, remove }
