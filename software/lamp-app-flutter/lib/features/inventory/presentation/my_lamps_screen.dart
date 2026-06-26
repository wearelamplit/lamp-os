import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/critter_icon.dart';
import '../../../core/widgets/inactive_backdrop_scrim.dart';
import '../../../core/widgets/status_dot.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../inventory/application/active_lamp_notifier.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../inventory/domain/inventory_lamp.dart';
import '../../inventory/domain/lamp_colors.dart';
import '../../lamp_shell/application/lamp_status.dart';
import '../../nearby/application/lamp_route_resolver.dart';
import '../../nearby/application/nearby_lamps_notifier.dart';
import '../../nearby/application/scan_grace_provider.dart';
import '../../nearby/domain/nearby_lamp.dart';
import '../domain/last_seen.dart';

/// Unified lamp picker — the app's landing screen for users with at least
/// one lamp, and also the destination of LampShell's "switch lamp" action.
///
/// Replaces the old split between MyLampsScreen and LampPickerSheet modal:
/// one full-screen widget, one ordered list (no online/offline section
/// headers), both delete affordances (swipe + long-press), and an "Adopt
/// a lamp" entry at the end. Tile order is connected → in-range → most-
/// recently-seen → alphabetical so the "online" lamps float to the top
/// without needing a structural split.
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

    final ordered = _sortInventory(inventory, nearbyById, activeId);

    return Scaffold(
      appBar: AppBar(
        title: const Text('My lamps'),
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
          children: [
            for (final lamp in ordered)
              _LampTile(
                lamp: lamp,
                nearbyById: nearbyById,
                isCurrent: lamp.id == activeId,
                inScanGrace: inScanGrace,
              ),
            const SizedBox(height: 8),
            const Divider(color: BrandColors.slateGrey, height: 1),
            const SizedBox(height: 8),
            const _AddLampTile(),
          ],
        ),
      ),
    );
  }
}

/// Order: connected first → in-range (mesh or bluetooth) → most-recently-
/// seen → alphabetical by name. Inventory's natural order is the
/// tiebreaker for entries with identical last-seen timestamps (so two
/// never-seen lamps fall back to their adoption order).
List<InventoryLamp> _sortInventory(
  List<InventoryLamp> inv,
  Map<String, NearbyLamp> nearbyById,
  String? activeId,
) {
  int rank(InventoryLamp l) {
    if (l.id == activeId) return 0;
    final hit = nearbyById[l.id];
    if (hit != null) return hit.isMesh ? 1 : 2;
    return 3;
  }

  final sorted = [...inv];
  sorted.sort((a, b) {
    final ra = rank(a);
    final rb = rank(b);
    if (ra != rb) return ra - rb;
    final aSeen = a.lastSeenEpochMs ?? 0;
    final bSeen = b.lastSeenEpochMs ?? 0;
    if (aSeen != bSeen) return bSeen.compareTo(aSeen);
    return a.name.toLowerCase().compareTo(b.name.toLowerCase());
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
    // Tapping any tile — including the currently-active lamp — navigates to
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
      backgroundColor: BrandColors.midnightBlack,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.lock_reset, color: BrandColors.fogGrey),
              title: const Text('Reset connection password',
                  style: TextStyle(color: BrandColors.lampWhite)),
              subtitle: const Text(
                'Use if Save / rename keeps reverting on this lamp.',
                style: TextStyle(color: BrandColors.slateGrey, fontSize: 12),
              ),
              onTap: () => Navigator.pop(ctx, _LampAction.resetPassword),
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
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Connected lamps don't advertise (NimBLE peripheral default), so the
    // BLE scan never sees them — they'd render as "offline" if we only
    // looked at `nearbyById`. Cross-check fbp's connectedDevices list to
    // catch that case. `isConnected` is a synchronous check against the
    // already-cached connected-devices set; it doesn't materialise any
    // notifier, so this stays cheap across an N-tile inventory.
    final bleClient = ref.watch(bleClientProvider);
    final connectedToThisLamp = bleClient.isConnected(lamp.id);
    // For the active lamp, also fold in the controlNotifier's `connected`
    // signal — fbp may report connected mid-handshake before the app has
    // a usable GATT session. The `.select` keeps slider drags from
    // rebuilding this tile.
    final notifierConnected = isCurrent &&
        ref.watch(controlNotifierProvider(lamp.id).select(
          (async) => async.value?.connected ?? false,
        ));
    final status = statusForById(
      lampId: lamp.id,
      nearbyById: nearbyById,
      connected: connectedToThisLamp || notifierConnected,
      inScanGrace: inScanGrace,
    );
    final hit = nearbyById[lamp.id];
    final colors = resolveLampColors(inv: lamp, near: hit);

    // Discoverable delete via swipe-left (audit ux-H3) + long-press for
    // back-compat with users who learned that gesture.
    return Dismissible(
      key: ValueKey(lamp.id),
      direction: DismissDirection.endToStart,
      background: Container(
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.symmetric(horizontal: 24),
        color: BrandColors.error,
        child: const Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.delete_outline, color: BrandColors.lampWhite),
            SizedBox(width: 8),
            Text(
              'Remove',
              style: TextStyle(
                color: BrandColors.lampWhite,
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
      confirmDismiss: (_) async {
        return await _confirmRemoveDialog(context, lamp.name);
      },
      onDismissed: (_) async {
        // Swipe is the only remove path. confirmDismiss already prompted, so
        // no dialog here — just remove and, if this was the active lamp,
        // repoint activeLampNotifier so nothing dangles at a deleted id.
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
      },
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        // Every tile is tappable, even offline ones — the user may want
        // to navigate to a lamp's screen to wait for its reconnect or
        // see its last-known state, even when not currently in range.
        onTap: () => _onTap(context, ref),
        onLongPress: () => _showLampActions(context, ref),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 12),
          child: Row(
            children: [
              StatusDot(kind: status, size: 14),
              const SizedBox(width: 12),
              CritterIcon(
                critterIndex: lamp.critterIndex,
                deviceId: lamp.id,
                shade: colors.shade ?? BrandColors.slateGrey,
                base: colors.base ?? BrandColors.slateGrey,
                size: 44,
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Text(
                  lamp.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: BrandColors.lampWhite,
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
              // Right-side detail. In advanced mode (session-unlock for
              // this lamp), the firmware/version line replaces the
              // status text — the operator already knows the lamp is
              // in range from the dot, and the version is the more
              // valuable info. Plain widget (no Flexible) so it pins to
              // the right edge; the Expanded name above absorbs the
              // remaining space and ellipsizes if needed.
              if (ref.watch(advancedSessionProvider(lamp.id)))
                ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 160),
                  child: _FirmwareInfo(lamp: lamp),
                )
              else
                Text(
                  _subtitle(status, lamp),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: BrandColors.slateGrey,
                    fontSize: 12,
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }

  String _subtitle(StatusKind status, InventoryLamp lamp) {
    switch (status) {
      case StatusKind.mesh:
        return 'In range';
      case StatusKind.bluetooth:
        return 'Bluetooth only';
      case StatusKind.searching:
        return 'Searching…';
      case StatusKind.offline:
        if (lamp.lastSeenEpochMs == null) return 'Not seen yet';
        return formatLastSeen(lamp.lastSeenEpochMs!, DateTime.now());
    }
  }
}

/// Single-line firmware identity shown in the tile when the lamp is
/// in advanced mode. Reads from inventory's persisted fwVersion /
/// fwChannel (mirrored from the live LampSection in ControlNotifier)
/// so it renders for offline lamps too.
///
/// Format: `{major}.{minor}.{patch}-{channel} ({lampType})`, e.g.
/// `1.0.82-stable (standard)`. Pieces with missing data are dropped:
/// no channel → `1.0.82 (standard)`; no type → `1.0.82-stable`; no
/// version → render `—` so the column collapses politely.
class _FirmwareInfo extends StatelessWidget {
  const _FirmwareInfo({required this.lamp});
  final InventoryLamp lamp;

  String _formatVersion(int? packed) {
    if (packed == null) return '';
    final major = (packed >> 16) & 0xFF;
    final minor = (packed >> 8) & 0xFF;
    final patch = packed & 0xFF;
    return '$major.$minor.$patch';
  }

  String _channelTail(String? raw) {
    if (raw == null || raw.isEmpty) return '';
    // v0x04 channels carry `{lampType}-{channel}`. Strip the variant
    // prefix so the user sees `stable` / `beta`, not `standard-stable`.
    return raw.contains('-') ? raw.split('-').last : raw;
  }

  @override
  Widget build(BuildContext context) {
    final version = _formatVersion(lamp.fwVersion);
    final channel = _channelTail(lamp.fwChannel);
    final type = (lamp.lampType ?? '').trim();

    final buf = StringBuffer();
    if (version.isEmpty) {
      buf.write('—');
    } else {
      buf.write(version);
      if (channel.isNotEmpty) buf..write('-')..write(channel);
      if (type.isNotEmpty) buf..write(' (')..write(type)..write(')');
    }

    return Text(
      buf.toString(),
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: const TextStyle(
        color: BrandColors.slateGrey,
        fontSize: 12,
      ),
    );
  }
}

/// Catch-all "Adopt a lamp" tile rendered after the inventory list.
class _AddLampTile extends StatelessWidget {
  const _AddLampTile();

  @override
  Widget build(BuildContext context) {
    return InkWell(
      borderRadius: BorderRadius.circular(12),
      onTap: () => GoRouter.maybeOf(context)?.push(AppRoutes.addLamp),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 14),
        child: Row(
          children: [
            Container(
              width: 44,
              height: 44,
              decoration: BoxDecoration(
                color: BrandColors.slateGrey.withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Icon(Icons.add, color: BrandColors.lampWhite),
            ),
            const SizedBox(width: 14),
            const Expanded(
              child: Text(
                'Adopt a lamp',
                style: TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
            const Icon(Icons.chevron_right, color: BrandColors.slateGrey),
          ],
        ),
      ),
    );
  }
}

/// Confirmation dialog used by both the swipe and long-press delete paths.
Future<bool> _confirmRemoveDialog(BuildContext context, String lampName) async {
  final ok = await showBlurredDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Remove this lamp?',
          style: TextStyle(color: BrandColors.lampWhite)),
      content: Text(
        '$lampName will be removed from your lamps on this phone. '
        "The lamp itself keeps its name, password, and Wi-Fi — you can "
        'add it back later from the picker.',
        style: const TextStyle(color: BrandColors.fogGrey),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Cancel'),
        ),
        FilledButton(
          style: FilledButton.styleFrom(backgroundColor: BrandColors.error),
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Remove'),
        ),
      ],
    ),
  );
  return ok == true;
}

enum _LampAction { resetPassword }
