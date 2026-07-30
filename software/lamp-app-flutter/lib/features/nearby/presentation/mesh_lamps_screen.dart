import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:lamp_app/core/utils/string_case.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/app_channel.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/critter_icon.dart';
import '../../../core/widgets/lamp_card.dart';
import '../../../core/widgets/section_header.dart';
import '../../control/application/control_notifier.dart';
import '../../control/domain/lamp_color.dart';
import '../../inventory/application/active_lamp_notifier.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../social/application/lamp_nearby_peers_notifier.dart';
import '../../social/domain/lamp_nearby_peer.dart';
import '../../wisp/application/wisp_notifier.dart';

/// Read-only view of the connected lamp's full mesh roster: the peers it
/// hears over ESP-NOW. Debug/monitor surface: shows firmware version and
/// OTA state per peer. No OTA push actions here. The connected lamp itself
/// is pinned at the top as "this lamp".
class MeshLampsScreen extends ConsumerWidget {
  const MeshLampsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final activeLamp = ref.watch(activeLampNotifierProvider);
    return Scaffold(
      appBar: AppBar(
        leading: const BackButtonLeading(),
        title: const Text('Lamp Network'),
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

/// The wisp's live base + shade paint for one lamp.
typedef WispPaint = ({LampColor base, LampColor shade});

List<int> _rgbwOf(LampColor? c) =>
    c == null ? const [0, 0, 0, 0] : [c.r, c.g, c.b, c.w];

class _Roster extends ConsumerWidget {
  const _Roster({required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final peersAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    if (!peersAsync.hasValue) {
      return const Center(child: CircularProgressIndicator(strokeWidth: 2));
    }
    final inventory = ref.watch(inventoryNotifierProvider).value;
    final self = inventory?.firstWhereOrNull((l) => l.id == lampId);
    // Resolve a sender's OTA target MAC to an inventory lamp by lampId
    // (case-insensitive; null until a lamp's first connect, so a
    // never-connected receiver resolves to its raw MAC).
    String? receiverLabel(LampNearbyPeer p) {
      final mac = p.otaSendingTo;
      if (mac == null) return null;
      final match = inventory?.firstWhereOrNull(
        (l) => l.lampId != null && l.lampId!.toLowerCase() == mac.toLowerCase(),
      );
      return match?.name.toTitleCase() ?? mac;
    }
    // A lamp receiving an OTA is HELLO-silent, so it can't self-report.
    // Derive the inbound edge: map each sender's target MAC to the sender's
    // name; the targeted peer renders an "OTA <-" pill.
    final receivingFrom = <String, String>{};
    for (final p in peersAsync.value!) {
      final target = p.otaSendingTo;
      if (target != null && target.isNotEmpty) {
        receivingFrom[target.toLowerCase()] = p.name.toTitleCase();
      }
    }
    String? senderLabel(LampNearbyPeer p) =>
        p.lampId.isEmpty ? null : receivingFrom[p.lampId.toLowerCase()];

    // Reuse the wisp notifier's live paint; a lamp shows the wisp glyph only
    // while the wisp is actively painting it (a live entry keyed by mesh MAC).
    // No new comms.
    ref.watch(wispNotifierProvider(lampId));
    final wisp = ref.read(wispNotifierProvider(lampId).notifier);
    final livePaint = wisp.livePaint;
    WispPaint? wispPaint(LampNearbyPeer p) =>
        p.lampId.isEmpty ? null : livePaint[p.lampId.toUpperCase()];
    final selfOta = ref.watch(
      controlNotifierProvider(lampId).select((s) => s.value?.lamp.otaState ?? 0),
    );
    final selfOtaSendingTo = ref.watch(
      controlNotifierProvider(lampId).select((s) => s.value?.lamp.otaSendingTo),
    );
    final selfColors = ref.watch(
      controlNotifierProvider(lampId).select(
        (s) => (base: s.value?.base.colors.firstOrNull, shade: s.value?.shade.colors.firstOrNull),
      ),
    );
    final selfPeer = self == null
        ? null
        : LampNearbyPeer(
            name: self.name,
            lampId: self.lampId ?? '',
            fwVersion: self.fwVersion ?? 0,
            fwChannel: self.fwChannel ?? '',
            otaState: selfOta,
            otaSendingTo: selfOtaSendingTo,
            baseRgbw: _rgbwOf(selfColors.base),
            shadeRgbw: _rgbwOf(selfColors.shade),
          );

    final peers = [...peersAsync.value!]
      ..sort((a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()));
    bool isBtOnly(LampNearbyPeer p) => p.viaBle && !p.viaEspNow;
    final btOnly = peers.where(isBtOnly).toList()
      ..sort((a, b) => b.rssi.compareTo(a.rssi));
    final mesh = peers.where((p) => !isBtOnly(p)).toList();

    return ListView(
      padding: const EdgeInsets.all(AppSpace.lg),
      children: [
        if (selfPeer != null) ...[
          const SectionHeader('This lamp'),
          const SizedBox(height: AppSpace.xs),
          _PeerCard(
            peer: selfPeer,
            isSelf: true,
            sendingTo: receiverLabel(selfPeer),
            receivingFrom: senderLabel(selfPeer),
            wispPaint: wispPaint(selfPeer),
          ),
          const SizedBox(height: AppSpace.sm),
        ],
        if (mesh.isNotEmpty) ...[
          const SectionHeader('Mesh'),
          const SizedBox(height: AppSpace.xs),
          for (final peer in mesh) ...[
            _PeerCard(
              peer: peer,
              sendingTo: receiverLabel(peer),
              receivingFrom: senderLabel(peer),
              wispPaint: wispPaint(peer),
            ),
            const SizedBox(height: AppSpace.sm),
          ],
        ],
        if (btOnly.isNotEmpty) ...[
          const SectionHeader('BT-only'),
          const SizedBox(height: AppSpace.xs),
          for (final peer in btOnly) ...[
            _PeerCard(
              peer: peer,
              sendingTo: receiverLabel(peer),
              receivingFrom: senderLabel(peer),
              wispPaint: wispPaint(peer),
              legacyOnlyBle: true,
            ),
            const SizedBox(height: AppSpace.sm),
          ],
        ],
        if (peers.isEmpty)
          Text(
            'No other lamps on the network yet.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
                  color: Theme.of(context).colorScheme.onSurfaceVariant,
                ),
          ),
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
  const _PeerCard({
    required this.peer,
    this.isSelf = false,
    this.sendingTo,
    this.receivingFrom,
    this.wispPaint,
    this.legacyOnlyBle = false,
  });
  final LampNearbyPeer peer;
  final bool isSelf;
  /// Resolved receiver name/MAC when this peer is OTA-sending; null otherwise.
  final String? sendingTo;
  /// Resolved sender name when another peer is OTA-pushing to this one; null
  /// otherwise. Derived, since the receiver is HELLO-silent during its update.
  final String? receivingFrom;
  /// The wisp's live base + shade paint for this lamp. Non-null only while the
  /// wisp is actively painting it; renders the two-orb wisp glyph.
  final WispPaint? wispPaint;
  final bool legacyOnlyBle;

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    final colorScheme = Theme.of(context).colorScheme;
    final pill = otaPillFor(peer,
        sendingTo: sendingTo, receivingFrom: receivingFrom);
    final base = displayRgbw(peer.baseRgbw, legacyOnlyBle: legacyOnlyBle);
    final shade = displayRgbw(peer.shadeRgbw, legacyOnlyBle: legacyOnlyBle);
    // critterAssetFor is deterministic for any string, so a never-connected
    // bare-MAC (or empty-id) peer still resolves to a stable critter shape.
    final identity = peer.lampId.isEmpty ? peer.name : peer.lampId;
    final fwText = fwLabel(peer.fwVersion, peer.fwChannel);
    return LampCard(
      padding: const EdgeInsets.all(AppSpace.md),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          CritterIcon(
            deviceId: identity,
            shade: _swatchOf(shade),
            base: _swatchOf(base),
            size: 38,
          ),
          const SizedBox(width: AppSpace.md),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  peer.name.isEmpty ? '(unnamed)' : peer.name.toTitleCase(),
                  style: textTheme.titleMedium,
                ),
                const SizedBox(height: AppSpace.xs),
                Text(
                  isSelf
                      ? (peer.lampId.isEmpty
                          ? 'this lamp'
                          : '${peer.lampId} · this lamp')
                      : '${peer.lampId} · ${peer.rssi} dBm',
                  style: textTheme.bodySmall?.copyWith(
                    fontFamily: 'monospace',
                    fontSize: 11,
                  ),
                ),
                // fw text left, OTA pill right on one row. The Expanded keeps
                // the pill's presence from reflowing the text above it.
                const SizedBox(height: AppSpace.xs),
                Row(
                  children: [
                    Expanded(
                      child: Text(
                        fwText,
                        style: textTheme.bodySmall?.copyWith(fontSize: 11),
                      ),
                    ),
                    if (pill != null)
                      Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: AppSpace.sm,
                          vertical: AppSpace.xs,
                        ),
                        decoration: BoxDecoration(
                          borderRadius: BorderRadius.circular(999),
                          color: (pill.isSend
                                  ? colorScheme.tertiary
                                  : colorScheme.secondary)
                              .withValues(alpha: 0.18),
                        ),
                        child: Text(
                          pill.label,
                          style: TextStyle(
                            fontSize: 10,
                            color: pill.isSend
                                ? colorScheme.onTertiaryContainer
                                : colorScheme.secondary,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
          if (wispPaint != null) ...[
            const SizedBox(width: AppSpace.sm),
            _WispDots(base: wispPaint!.base, shade: wispPaint!.shade),
          ],
        ],
      ),
    );
  }

  Color _swatchOf(List<int> rgbw) => LampColor(
        r: rgbw.isNotEmpty ? rgbw[0] : 0,
        g: rgbw.length > 1 ? rgbw[1] : 0,
        b: rgbw.length > 2 ? rgbw[2] : 0,
        w: rgbw.length > 3 ? rgbw[3] : 0,
      ).toSwatch();
}

/// Two small orbs tinted with the wisp's base + shade paint, diagonally
/// offset like the will-o'-wisp glyph on the control screen (shade up/left,
/// base down/right) so the same wisp reads alike here.
class _WispDots extends StatelessWidget {
  const _WispDots({required this.base, required this.shade});
  final LampColor base;
  final LampColor shade;

  @override
  Widget build(BuildContext context) => Semantics(
        label: 'Wisp-painted',
        child: SizedBox(
          width: 22,
          height: 22,
          child: Stack(
            children: [
              Positioned(top: 0, right: 0, child: _dot(shade.toSwatch())),
              Positioned(bottom: 0, left: 0, child: _dot(base.toSwatch())),
            ],
          ),
        ),
      );

  Widget _dot(Color color) => Container(
        width: 13,
        height: 13,
        decoration: BoxDecoration(shape: BoxShape.circle, color: color),
      );
}

/// OTA pill content for a peer row. `sendingTo` names the receiver when this
/// peer is the sender; `receivingFrom` names the sender when another peer is
/// OTA-pushing to this one (derived, the receiver is HELLO-silent). Returns
/// null when no OTA edge touches this peer. `isSend` picks the accent: send is
/// the sourcing color, receive is the amber "updating" role.
typedef OtaPill = ({String label, bool isSend});

/// Firmware line for a peer row. A reported version renders verbatim (`fw
/// 1.2.3`, plus ` · channel` when present). `0.0.1` is the deliberate
/// signature of a faux-mesh OTA-catch legacy lamp (upgradeable) and shows as
/// such. A peer that reports no version (0) is pre-mesh BLE-only with no OTA
/// path; it shows a dash so the two are distinguishable.
String fwLabel(int fwVersion, String fwChannel) {
  if (fwVersion == 0) return 'fw —';
  final semver = formatFirmwareSemver(fwVersion);
  return fwChannel.isEmpty ? 'fw $semver' : 'fw $semver · $fwChannel';
}

OtaPill? otaPillFor(
  LampNearbyPeer peer, {
  String? sendingTo,
  String? receivingFrom,
}) {
  if (sendingTo != null || peer.otaState == 1) {
    return (label: sendingTo != null ? '→ $sendingTo' : '→', isSend: true);
  }
  if (receivingFrom != null || peer.otaState == 2) {
    return (
      label: receivingFrom != null ? '← $receivingFrom' : '←',
      isSend: false,
    );
  }
  return null;
}
