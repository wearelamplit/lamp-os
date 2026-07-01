import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/lamp_color_swatch.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../inventory/domain/inventory_lamp.dart';
import '../../../social/application/lamp_nearby_peers_notifier.dart';
import '../../../social/domain/lamp_nearby_peer.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/tuple_sampler.dart';
import '../../domain/wisp_source_mode.dart';

class PaintedLampEntry {
  const PaintedLampEntry({required this.bdAddr, required this.name});
  final String bdAddr;
  final String name;
}

// Membership is the claimed bdAddr set (all claimed lamps show). Name is
// resolved from the connected lamp's nearby peers; a lamp never lists itself
// as a peer, so the connected lamp's own (selfBdAddr, selfName) is seeded too.
// An unresolved claim shows with its last two bdAddr octets, never dropped.
List<PaintedLampEntry> resolvePaintedLamps({
  required Set<String>? claimed,
  required List<LampNearbyPeer> peers,
  String? selfBdAddr,
  String? selfName,
}) {
  if (claimed == null) return const [];
  final byBd = {for (final p in peers) p.bdAddr.toUpperCase(): p.name};
  if (selfBdAddr != null && selfName != null && selfName.isNotEmpty) {
    byBd[selfBdAddr.toUpperCase()] = selfName;
  }
  return [
    for (final bd in claimed)
      PaintedLampEntry(
        bdAddr: bd,
        name: byBd[bd.toUpperCase()] ??
            'Lamp ${bd.length >= 5 ? bd.substring(bd.length - 5) : bd}',
      ),
  ];
}

/// The palette the per-lamp preview should sample. Only Manual mode paints a
/// palette the app can predict per lamp: Off paints nothing to the grid (the
/// offColor lives only on the wisp's own ring) and the Aurora palette isn't
/// carried to the app, so both yield an empty palette and the rows render
/// dashes -- accurately showing the grid is getting nothing the app can read.
List<LampColor> previewPaletteFor(
        WispSourceMode? source, List<LampColor> manual) =>
    source == WispSourceMode.manual ? manual : const <LampColor>[];

// Lists lamps the wisp currently claims, with the two colors it paints on each
// (base + shade). Membership comes from CHAR_WISP_CLAIMS (bdAddr set); names
// resolve from the connected lamp's nearby-peer report.
class PaintedLampsList extends ConsumerWidget {
  const PaintedLampsList({super.key, required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(wispNotifierProvider(lampId).notifier);
    final status = ref.watch(wispNotifierProvider(lampId)).value;
    final shuffleSeed = status?.shuffleSeed ?? 0;
    final palette = previewPaletteFor(status?.source, notifier.savedManualPalette);
    final claimedMacs = notifier.claimedMacs; // Set<String>? of bdAddrs
    final peersAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    final peers = peersAsync.value ?? const <LampNearbyPeer>[];

    // The connected lamp never lists itself as a nearby peer, so supply its
    // own name from inventory (on Android lampId IS the bdAddr) — otherwise
    // its own claimed entry would render as a bare bdAddr label.
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const <InventoryLamp>[];
    String? selfName;
    for (final l in inventory) {
      if (l.id == lampId) {
        selfName = l.name;
        break;
      }
    }

    // claimedMacs == null: claims unavailable (legacy lamp / timeout) -> show
    // every nearby peer rather than blocking or hiding.
    final entries = claimedMacs == null
        ? [for (final p in peers) PaintedLampEntry(bdAddr: p.bdAddr, name: p.name)]
        : resolvePaintedLamps(
            claimed: claimedMacs,
            peers: peers,
            selfBdAddr: lampId,
            selfName: selfName,
          );

    if (entries.isEmpty) {
      return Padding(
        padding:
            const EdgeInsets.symmetric(horizontal: 16, vertical: AppSpace.md),
        child: Text('No lamps claimed by this wisp right now.',
            style: Theme.of(context).textTheme.bodySmall),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (final e in entries)
          _PaintedLampRow(
            bdAddr: e.bdAddr,
            name: e.name,
            palette: palette,
            shuffleSeed: shuffleSeed,
          ),
      ],
    );
  }
}

class _PaintedLampRow extends StatelessWidget {
  const _PaintedLampRow({
    required this.bdAddr,
    required this.name,
    required this.palette,
    required this.shuffleSeed,
  });

  final String bdAddr;
  final String name;
  final List<LampColor> palette;
  final int shuffleSeed;

  @override
  Widget build(BuildContext context) {
    final mac = meshMacFromBdAddr(bdAddr);
    final prediction = (mac == null || palette.isEmpty)
        ? null
        : predictTuple(mac: mac, palette: palette, shuffleSeed: shuffleSeed);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      child: Row(
        children: [
          Expanded(
            child: Text(
              name,
              style: Theme.of(context).textTheme.bodyLarge?.copyWith(
                fontWeight: FontWeight.w500,
                fontSize: 14,
              ),
              overflow: TextOverflow.ellipsis,
            ),
          ),
          if (prediction == null)
            Text(
              '—',
              style: Theme.of(context).textTheme.bodySmall,
            )
          else ...[
            // Tooltip hex for color-blind operators; RGB-only because
            // the wisp wire palette has no W channel.
            Tooltip(
              message: 'base #${prediction.base.toRgbHex()}',
              child: LampColorSwatch(
                color: prediction.base,
                size: 22,
                shape: LampSwatchShape.roundedSquare,
                borderRadius: 6,
              ),
            ),
            const SizedBox(width: 6),
            Tooltip(
              message: 'shade #${prediction.shade.toRgbHex()}',
              child: LampColorSwatch(
                color: prediction.shade,
                size: 22,
                shape: LampSwatchShape.roundedSquare,
                borderRadius: 6,
              ),
            ),
          ],
        ],
      ),
    );
  }
}
