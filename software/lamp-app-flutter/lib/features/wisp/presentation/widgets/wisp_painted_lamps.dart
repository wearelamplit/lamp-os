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

/// Selects the paint color for a lamp row. Live color wins over prediction;
/// returns null when neither is available (empty palette + no live entry).
({LampColor base, LampColor shade})? paintColorFor({
  required String lampId,
  required Map<String, ({LampColor base, LampColor shade})> livePaint,
  required List<LampColor> palette,
  required int shuffleSeed,
}) {
  final live = livePaint[lampId.toUpperCase()];
  if (live != null) return live;
  final mac = parseMacFromBleId(lampId);
  if (mac == null || palette.isEmpty) return null;
  final p = predictTuple(mac: mac, palette: palette, shuffleSeed: shuffleSeed);
  if (p == null) return null;
  return (base: p.base, shade: p.shade);
}

class PaintedLampEntry {
  const PaintedLampEntry({required this.lampId, required this.name});
  final String lampId;
  final String name;
}

// Membership is the claimed mac set (all claimed lamps show). Name is
// resolved from the connected lamp's nearby peers; a lamp never lists itself
// as a peer, so the connected lamp's own (selfLampId, selfName) is seeded too.
// An unresolved claim shows with its last two mac octets, never dropped.
List<PaintedLampEntry> resolvePaintedLamps({
  required Set<String>? claimed,
  required List<LampNearbyPeer> peers,
  String? selfLampId,
  String? selfName,
}) {
  if (claimed == null) return const [];
  final byId = {for (final p in peers) p.lampId.toUpperCase(): p.name};
  if (selfLampId != null && selfName != null && selfName.isNotEmpty) {
    byId[selfLampId.toUpperCase()] = selfName;
  }
  return [
    for (final id in claimed)
      PaintedLampEntry(
        lampId: id,
        name: byId[id.toUpperCase()] ??
            'Lamp ${id.length >= 5 ? id.substring(id.length - 5) : id}',
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
// (base + shade). Membership comes from CHAR_WISP_CLAIMS (mesh mac set); names
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
    final claimedMacs = notifier.claimedMacs;
    final livePaint = notifier.livePaint;
    final peersAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    final peers = peersAsync.value ?? const <LampNearbyPeer>[];

    // The connected lamp never lists itself as a nearby peer, so supply its
    // own name from inventory, keyed by the connected lamp's own mesh mac
    // (InventoryLamp.lampId), not the BLE connection id; otherwise its own
    // claimed entry would render as a bare mac label.
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const <InventoryLamp>[];
    String? selfName;
    String? selfLampId;
    for (final l in inventory) {
      if (l.id == lampId) {
        selfName = l.name;
        selfLampId = l.lampId;
        break;
      }
    }

    // claimedMacs == null: claims unavailable (legacy lamp / timeout) -> show
    // every nearby peer rather than blocking or hiding.
    final entries = claimedMacs == null
        ? [for (final p in peers) PaintedLampEntry(lampId: p.lampId, name: p.name)]
        : resolvePaintedLamps(
            claimed: claimedMacs,
            peers: peers,
            selfLampId: selfLampId,
            selfName: selfName,
          );

    if (entries.isEmpty) {
      return Padding(
        padding:
            const EdgeInsets.symmetric(horizontal: AppSpace.lg, vertical: AppSpace.md),
        child: Text('No lamps claimed by this wisp right now.',
            style: Theme.of(context).textTheme.bodySmall),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        for (final e in entries)
          _PaintedLampRow(
            lampId: e.lampId,
            name: e.name,
            palette: palette,
            shuffleSeed: shuffleSeed,
            livePaint: livePaint,
          ),
      ],
    );
  }
}

class _PaintedLampRow extends StatelessWidget {
  const _PaintedLampRow({
    required this.lampId,
    required this.name,
    required this.palette,
    required this.shuffleSeed,
    required this.livePaint,
  });

  final String lampId;
  final String name;
  final List<LampColor> palette;
  final int shuffleSeed;
  final Map<String, ({LampColor base, LampColor shade})> livePaint;

  @override
  Widget build(BuildContext context) {
    final colors = paintColorFor(
      lampId: lampId,
      livePaint: livePaint,
      palette: palette,
      shuffleSeed: shuffleSeed,
    );
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: AppSpace.lg, vertical: AppSpace.sm),
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
          if (colors == null)
            Text(
              '-',
              style: Theme.of(context).textTheme.bodySmall,
            )
          else ...[
            // Tooltip hex for color-blind operators.
            Tooltip(
              message: 'base ${colors.base.toHex()}',
              child: LampColorSwatch(
                color: colors.base,
                size: 22,
                shape: LampSwatchShape.roundedSquare,
                borderRadius: 6,
              ),
            ),
            const SizedBox(width: 6),
            Tooltip(
              message: 'shade ${colors.shade.toHex()}',
              child: LampColorSwatch(
                color: colors.shade,
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
