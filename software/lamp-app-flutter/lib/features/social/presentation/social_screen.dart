import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/ble/ble_client_provider.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/utils/tap_counter.dart';
import '../../../core/widgets/critter_icon.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/section_header.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../wisp/data/wisp_repository.dart';
import '../application/dispositions_notifier.dart';
import '../application/lamp_nearby_peers_notifier.dart';
import '../domain/lamp_nearby_peer.dart';
import '../domain/social_mode.dart';

/// Social tab: lamp personality + per-peer disposition list.
///
/// Top: a row of chunky personality pills (Introvert / Ambivert /
/// Extrovert), same visual style as the Shade/Base/Both picker in the
/// add-expression flow. Bound to ControlNotifier.setLampSocialMode —
/// persists immediately via `writeSettingsBlob` (`reboot:false`). No timings
/// exposed, whimsical-by-design.
///
/// Below: every lamp currently nearby (live BLE). Disposition is only
/// meaningful when the peer is actually here — historical "seen" lamps
/// are not shown here. Each row carries a 5-position slider with a
/// single active-position label on the right (salty → wary →
/// neutral → fond → smitten) wired to the dispositions provider.
/// Writes are debounced 500ms after the last slider movement and
/// flushed on tab leave so a drag-then-navigate doesn't drop the edit.
class SocialScreen extends ConsumerWidget {
  const SocialScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // .select() so this screen only rebuilds when (mode, selfName, hasState)
    // changes, NOT on every slider tick or color drag that goes through
    // controlNotifierProvider.
    final ctl = ref.watch(controlNotifierProvider(lampId).select((a) {
      final lamp = a.value?.lamp;
      return (
        hasState: lamp != null,
        mode: lamp?.socialMode ?? SocialMode.ambivert,
        selfName: lamp?.name ?? '',
      );
    }));
    if (!ctl.hasState) {
      return const Center(
        child: CircularProgressIndicator(),
      );
    }
    final notifier = ref.read(controlNotifierProvider(lampId).notifier);
    final mode = ctl.mode;
    final selfName = ctl.selfName;

    // Source is the connected lamp's own nearby-peers view
    // (CHAR_NEARBY_LAMPS section), not the phone's direct BLE scan.
    // The user might be 30 ft from the connected lamp; what matters
    // for dispositions is what the LAMP can hear, not the phone.
    final nearbyAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    final List<LampNearbyPeer> nearby = nearbyAsync.value ?? const [];
    final nearbyLoaded = nearbyAsync.hasValue;
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const [];
    // Index inventory by id for the per-row name lookup below.
    final inventoryById = {for (final l in inventory) l.id: l};

    // Rows come from the connected lamp's own nearby JSON.
    // Self-filter: keep `l.name != selfName` (defense-in-depth — the
    // firmware already excludes self in buildNearbyLampsJson, but a
    // debug-inject path in standard_lamp.cpp could surface it).
    // Inventory cross-reference for display names: prefer inventory
    // .name over the lamp-emitted name when the BD_ADDR matches an
    // inventory entry's id (works on Android; iOS uses CoreBluetooth
    // UUIDs for inventory.id so cross-reference is a known iOS gap —
    // separate post-release task).
    final rows = <_SocialLampRow>[
      for (final l in nearby)
        if (l.name.isNotEmpty && l.name != selfName)
          _SocialLampRow(
            name: inventoryById[l.bdAddr]?.name ?? l.name,
            bdAddr: l.bdAddr,
            baseRgb: _rgbwToRgb(l.baseRgbw),
            shadeRgb: _rgbwToRgb(l.shadeRgbw),
            rssi: l.rssi,
            proximity: l.proximity,
            critterIndex: inventoryById[l.bdAddr]?.critterIndex,
          ),
    ];

    // Sort: Near (0) → Around (1) → Far (2), alphabetical within bucket.
    rows.sort((a, b) {
      final byBucket = a.proximity.compareTo(b.proximity);
      if (byBucket != 0) return byBucket;
      return a.name.toLowerCase().compareTo(b.name.toLowerCase());
    });

    final colorScheme = Theme.of(context).colorScheme;

    return DisconnectAwareBody(
      lampId: lampId,
      child: ListView(
      padding: const EdgeInsets.fromLTRB(AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xxl),
      children: [
        const SectionHeader('Personality'),
        const SizedBox(height: AppSpace.sm),
        Row(
          children: [
            _PersonalityButton(
              label: 'Introvert',
              icon: Icons.bedtime_outlined,
              selected: mode == SocialMode.introvert,
              onTap: () => notifier.setLampSocialMode(SocialMode.introvert),
            ),
            const SizedBox(width: AppSpace.sm),
            _PersonalityButton(
              label: 'Ambivert',
              icon: Icons.balance,
              selected: mode == SocialMode.ambivert,
              onTap: () => notifier.setLampSocialMode(SocialMode.ambivert),
            ),
            const SizedBox(width: AppSpace.sm),
            _PersonalityButton(
              label: 'Extrovert',
              icon: Icons.celebration_outlined,
              selected: mode == SocialMode.extrovert,
              onTap: () => notifier.setLampSocialMode(SocialMode.extrovert),
            ),
          ],
        ),
        const SizedBox(height: AppSpace.lg),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppSpace.xs),
          child: Text(
            'Lamps notice the company they keep. How this one greets, '
            'glows, and settles shifts a little with the lamps it meets — '
            'and with how it feels about each of them.',
            style: Theme.of(context).textTheme.bodySmall?.copyWith(
              fontStyle: FontStyle.italic,
              height: 1.4,
            ),
          ),
        ),
        const SizedBox(height: AppSpace.lg),
        const SectionHeader('Nearby lamps'),
        const SizedBox(height: AppSpace.xs),
        if (!nearbyLoaded)
          const Padding(
            padding: EdgeInsets.symmetric(vertical: AppSpace.xl),
            child: Center(
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
          )
        else if (rows.isEmpty)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: AppSpace.xl),
            child: EmptyStatePane(
              icon: Icon(
                Icons.sensors_off,
                size: 40,
                color: colorScheme.onSurfaceVariant,
              ),
              title: 'No lamps nearby',
              subtitle: "Once others wander by, they'll show up here.",
            ),
          )
        else
          for (final row in rows)
            _LampDispositionRow(lampId: lampId, row: row),
      ],
      ),
    );
  }
}

class _SocialLampRow {
  const _SocialLampRow({
    required this.name,
    required this.bdAddr,
    required this.baseRgb,
    required this.shadeRgb,
    required this.rssi,
    required this.proximity,
    required this.critterIndex,
  });
  final String name;
  final String bdAddr;
  final int baseRgb;
  final int shadeRgb;
  /// Persisted critter id (1..16) when this mesh peer is also in the
  /// phone's inventory; null for unknown peers (CritterIcon falls back
  /// to a deterministic hash on `bdAddr`).
  final int? critterIndex;
  // TEMP DEBUG: most recent BLE-scan RSSI from the lamp's perspective.
  // Used for the small subscript next to the proximity label. Strip
  // once bench tuning settles.
  final int rssi;
  /// Proximity bucket: 0=Near, 1=Around, 2=Far. Lamp-emitted via the
  /// nearby JSON's `proximity` field. Defaults to 2 (Far) on backward-compat.
  final int proximity;
}

class _LampDispositionRow extends ConsumerStatefulWidget {
  const _LampDispositionRow({required this.lampId, required this.row});
  final String lampId;
  final _SocialLampRow row;

  @override
  ConsumerState<_LampDispositionRow> createState() =>
      _LampDispositionRowState();
}

class _LampDispositionRowState extends ConsumerState<_LampDispositionRow> {
  // 3 taps on a peer's row within 2s fire a greeting from the connected
  // lamp toward that peer, so the user can trigger greetings on demand
  // without dragging physical peers in and out of BLE range.
  late final TapCounter _testGreetTaps = TapCounter(
    count: 3,
    window: const Duration(seconds: 2),
    onTriggered: _fireTestGreet,
  );

  bool _flashing = false;

  void _fireTestGreet() {
    final bdAddr = widget.row.bdAddr;
    if (bdAddr.isEmpty) return;
    final repo = WispRepository(ref.read(bleClientProvider), widget.lampId);
    repo.testGreet(bdAddr);
    HapticFeedback.mediumImpact();
    setState(() => _flashing = true);
    Future<void>.delayed(const Duration(milliseconds: 120), () {
      if (mounted) setState(() => _flashing = false);
    });
  }

  @override
  Widget build(BuildContext context) {
    final lampId = widget.lampId;
    final row = widget.row;
    // Dispositions key by BD_ADDR. Peers come from the connected lamp's
    // own nearby JSON (mesh peer table populated by ESP-NOW HELLOs), so
    // any peer the connected lamp has heard from will have a bdAddr.
    // Empty bdAddr is transient: the row was synthesised from a partial
    // entry (e.g. nearby roster entry seen via BLE adv before the peer's
    // first HELLO arrived). Self-corrects within a few seconds once the
    // mesh propagation catches up.
    final hasBdAddr = row.bdAddr.isNotEmpty;
    // Watch the AsyncValue (not just the notifier) so we can gate the
    // slider on hasValue — without this, during the BLE read in
    // Dispositions.build() every row paints at neutral (3) and then
    // "snaps" to the persisted value once the read completes, which
    // reads as jitter. Gate => render slider only once data lands.
    final dispositionsAsync = hasBdAddr
        ? ref.watch(dispositionsProvider(lampId))
        : null;
    final dispNotifier = hasBdAddr
        ? ref.read(dispositionsProvider(lampId).notifier)
        : null;
    final dispositionsLoaded = dispositionsAsync?.hasValue ?? false;
    final disposition =
        (hasBdAddr && dispositionsLoaded) ? dispNotifier!.get(row.bdAddr) : 3;

    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;

    return AnimatedContainer(
      duration: const Duration(milliseconds: 200),
      curve: Curves.easeOut,
      decoration: BoxDecoration(
        color: _flashing
            ? colorScheme.secondary.withValues(alpha: 0.25)
            : Colors.transparent,
        borderRadius: BorderRadius.circular(6),
      ),
      padding: const EdgeInsets.symmetric(vertical: AppSpace.md, horizontal: AppSpace.sm),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 3 taps anywhere on the name row fire a greeting — the whole row
          // is the hit target (opaque) so it's easy to land, not a tiny label.
          GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: _testGreetTaps.record,
            child: Row(
              children: [
                CritterIcon(
                  critterIndex: row.critterIndex,
                  deviceId: row.bdAddr,
                  shade: Color(0xFF000000 | row.shadeRgb),
                  base: Color(0xFF000000 | row.baseRgb),
                  size: 22,
                ),
                const SizedBox(width: AppSpace.md),
                Expanded(
                  child: Text(
                    row.name,
                    overflow: TextOverflow.ellipsis,
                    style: textTheme.titleMedium?.copyWith(fontSize: 14),
                  ),
                ),
                // Proximity bucket label is the user-visible signal. Raw dBm
                // subscript is debug-only — gated on advanced-mode session flag
                // and rendered TO THE LEFT of the label so the row stays a tidy
                // two lines (name row + slider row) regardless of mode.
                if (_advancedDbm(ref, lampId, row.rssi) case final dbm?)
                  Padding(
                    padding: const EdgeInsets.only(right: AppSpace.sm),
                    child: Text(
                      dbm,
                      style: textTheme.bodySmall?.copyWith(
                        color: colorScheme.secondary,
                        fontSize: 10,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                Text(
                  _proximityLabel(row.proximity),
                  style: textTheme.bodySmall,
                ),
              ],
            ),
          ),
          if (!hasBdAddr)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
              child: Text(
                'Waiting for peer info…',
                style: textTheme.bodySmall?.copyWith(
                  fontStyle: FontStyle.italic,
                ),
              ),
            )
          else if (!dispositionsLoaded)
            // BLE read of CHAR_SOCIAL_DISPOSITIONS still in flight —
            // hold the slider in its slot but blank until value lands,
            // so the row doesn't paint at neutral then snap to actual.
            const SizedBox(height: 48) // deliberate dimension, not spacing
          else
            Row(
              children: [
                Expanded(
                  child: Slider(
                    value: disposition.toDouble(),
                    min: 1,
                    max: 5,
                    divisions: 4,
                    onChanged: (v) =>
                        dispNotifier!.set(row.bdAddr, v.round()),
                  ),
                ),
                const SizedBox(width: AppSpace.sm),
                // Single active-position label — deliberately breaks the
                // usual "endpoint legend" pattern. Whimsical-by-design:
                // the word changes as you drag and reveals the continuum
                // through interaction rather than upfront labelling.
                SizedBox(
                  width: 72,
                  child: Text(
                    _dispositionLabel(disposition),
                    textAlign: TextAlign.right,
                    style: textTheme.titleMedium?.copyWith(fontSize: 13),
                  ),
                ),
              ],
            ),
        ],
      ),
    );
  }
}

/// Big chunky pill — same visual style as `_TargetButton` in
/// `add_expression_picker_screen.dart` (shade/base/both). Renders the
/// active mode with a primary fill so it reads at a glance.
class _PersonalityButton extends StatelessWidget {
  const _PersonalityButton({
    required this.label,
    required this.icon,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final IconData icon;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final fill = selected ? colorScheme.primary : Colors.transparent;
    final border = selected
        ? colorScheme.primary
        : colorScheme.outline;
    final fg = selected ? colorScheme.onPrimary : colorScheme.onSurface;
    return Expanded(
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(AppRadius.card),
          child: Container(
            height: 72,
            decoration: BoxDecoration(
              color: fill,
              border: Border.all(
                color: border,
                width: selected ? 2 : 1,
              ),
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, color: fg, size: 24),
                const SizedBox(height: AppSpace.xs),
                Text(
                  label,
                  style: TextStyle(
                    color: fg,
                    fontSize: 14,
                    fontWeight:
                        selected ? FontWeight.w700 : FontWeight.w500,
                    letterSpacing: 0.5,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Cute/playful escalation describing how the lamp feels about the named
/// peer — from "salty" (1) to "smitten" (5). Used as the single active-
/// position label on the disposition slider. Values outside 1..5 fall
/// back to neutral.
String _dispositionLabel(int value) {
  switch (value) {
    case 1:
      return 'salty';
    case 2:
      return 'wary';
    case 4:
      return 'fond';
    case 5:
      return 'smitten';
    case 3:
    default:
      return 'neutral';
  }
}

/// Collapses the firmware's 4-channel `[r,g,b,w]` per peer into a
/// 24-bit RGB int for the CritterIcon color params. White channel is
/// added to all three RGB channels (clamped to 255) — matches the
/// firmware's perceived-color rendering for LED preview.
int _rgbwToRgb(List<int> rgbw) {
  if (rgbw.length < 4) return 0;
  final r = (rgbw[0] + rgbw[3]).clamp(0, 255);
  final g = (rgbw[1] + rgbw[3]).clamp(0, 255);
  final b = (rgbw[2] + rgbw[3]).clamp(0, 255);
  return (r << 16) | (g << 8) | b;
}

/// Proximity bucket → user-visible label. Mirrors the
/// firmware-side `lamp::Proximity` enum order in `util/proximity.hpp`.
String _proximityLabel(int bucket) => switch (bucket) {
  0 => 'Near',
  1 => 'Around',
  2 => 'Far',
  _ => 'Far',
};

/// Returns the `"-72 dBm"` string when (a) the session is in advanced
/// mode for this lamp, AND (b) the peer has a real RSSI reading
/// (not the -127 sentinel). Returns null otherwise so the caller can
/// conditionally render via the `case ... ?` pattern.
String? _advancedDbm(WidgetRef ref, String lampId, int rssi) {
  if (rssi == -127) return null;
  final advanced = ref.watch(effectiveAdvancedProvider(lampId));
  if (!advanced) return null;
  return '$rssi dBm';
}
