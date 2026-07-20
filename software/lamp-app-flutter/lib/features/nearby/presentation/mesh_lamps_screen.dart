import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/app_channel.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/lamp_card.dart';
import '../../../core/widgets/section_header.dart';
import '../../inventory/application/active_lamp_notifier.dart';
import '../../social/application/lamp_nearby_peers_notifier.dart';
import '../../social/domain/lamp_nearby_peer.dart';

/// Read-only view of the connected lamp's full mesh roster: the peers it
/// hears over ESP-NOW. Debug/monitor surface: shows firmware version and
/// OTA state per peer. No OTA push actions here.
class MeshLampsScreen extends ConsumerWidget {
  const MeshLampsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final activeLamp = ref.watch(activeLampNotifierProvider);
    return Scaffold(
      appBar: AppBar(
        leading: const BackButtonLeading(),
        title: const Text('Mesh lamps'),
      ),
      body: switch (activeLamp) {
        AsyncData(:final value?) => _Roster(lampId: value),
        AsyncData() => const Center(child: Text('No lamp selected.')),
        _ => const Center(child: CircularProgressIndicator(strokeWidth: 2)),
      },
    );
  }
}

/// Mirrors the firmware `LampRoster::kCapacity`. At this count the roster is
/// full and may be hiding stalest-evicted peers, so the list gets a caption.
const int _rosterCap = 50;

class _Roster extends ConsumerWidget {
  const _Roster({required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final peersAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    if (!peersAsync.hasValue) {
      return const Center(child: CircularProgressIndicator(strokeWidth: 2));
    }
    final peers = [...peersAsync.value!]
      ..sort((a, b) => compareByProximityThenName(
            proximityFromRssi(a.rssi),
            a.name,
            proximityFromRssi(b.rssi),
            b.name,
          ));
    if (peers.isEmpty) {
      final colorScheme = Theme.of(context).colorScheme;
      return EmptyStatePane(
        icon: Icon(
          Icons.sensors_off,
          size: 40,
          color: colorScheme.onSurfaceVariant,
        ),
        title: 'No lamps on the mesh',
        subtitle: 'Peers this lamp hears will show up here.',
      );
    }

    bool isBtOnly(LampNearbyPeer p) => p.viaBle && !p.viaEspNow;
    final btOnly = peers.where(isBtOnly).toList()
      ..sort((a, b) => b.rssi.compareTo(a.rssi));

    final byTier = <int, List<LampNearbyPeer>>{};
    for (final p in peers) {
      if (isBtOnly(p)) continue;
      byTier.putIfAbsent(proximityFromRssi(p.rssi), () => []).add(p);
    }

    return ListView(
      padding: const EdgeInsets.all(AppSpace.lg),
      children: [
        for (final tier in const [0, 1, 2])
          if (byTier[tier] case final tierPeers?) ...[
            SectionHeader(proximityLabel(tier)),
            const SizedBox(height: AppSpace.xs),
            for (final peer in tierPeers) ...[
              _PeerCard(peer: peer),
              const SizedBox(height: AppSpace.sm),
            ],
          ],
        if (btOnly.isNotEmpty) ...[
          const SectionHeader('BT-only'),
          const SizedBox(height: AppSpace.xs),
          for (final peer in btOnly) ...[
            _PeerCard(peer: peer),
            const SizedBox(height: AppSpace.sm),
          ],
        ],
        if (peers.length >= _rosterCap) ...[
          const SizedBox(height: AppSpace.sm),
          Text(
            'Showing $_rosterCap nearby lamps — there may be more.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
          ),
        ],
      ],
    );
  }
}

class _PeerCard extends StatelessWidget {
  const _PeerCard({required this.peer});
  final LampNearbyPeer peer;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    final colorScheme = Theme.of(context).colorScheme;
    return LampCard(
      padding: const EdgeInsets.all(AppSpace.md),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  peer.name.isEmpty ? '(unnamed)' : peer.name,
                  style: textTheme.titleMedium,
                ),
              ),
              if (peer.otaState != 0)
                Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: AppSpace.sm,
                    vertical: AppSpace.xs,
                  ),
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(999),
                    color: colorScheme.primary.withValues(alpha: 0.18),
                  ),
                  child: Text(
                    _otaStateLabel(peer.otaState),
                    style: TextStyle(
                      fontSize: 10,
                      color: colorScheme.primary,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
            ],
          ),
          Text(
            '${peer.lampId} · ${peer.rssi} dBm',
            style: textTheme.bodySmall?.copyWith(
              fontFamily: 'monospace',
              fontSize: 11,
            ),
          ),
          if (peer.fwVersion != 0)
            Text(
              'fw ${formatFirmwareSemver(peer.fwVersion)}',
              style: textTheme.bodySmall?.copyWith(fontSize: 11),
            ),
        ],
      ),
    );
  }
}

String _otaStateLabel(int state) => switch (state) {
  1 => 'sending',
  2 => 'receiving',
  _ => 'idle',
};
