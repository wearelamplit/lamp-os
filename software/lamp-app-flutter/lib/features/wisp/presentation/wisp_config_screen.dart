import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/password_prompt_dialog.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/control_notifier.dart';
import '../../control/domain/lamp_color.dart';
import '../../control/presentation/widgets/color_picker_sheet.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import '../../control/presentation/widgets/lamp_color_swatch.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../inventory/domain/inventory_lamp.dart';
import '../../lamp_shell/presentation/widgets/wifi_network_picker.dart';
import '../application/wisp_notifier.dart';
import '../domain/tuple_sampler.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';
import '../domain/zone_source.dart';
import 'palette_gradient_bar.dart';

/// Wisp config: controls how the wisp drives the lamp grid's paint. Reached
/// from the WispIndicator's 5-tap-orbs gesture (the only entry point).
///
/// The wisp is a separate ESP32-C6 node that either follows an Aurora zone or
/// repaints the mesh from an operator palette. The app talks to it through the
/// lamp's BLE control service, which proxies wispOps onto the mesh and caches
/// the wisp's status back. `lampId` is that BLE proxy.
///
/// Layout: gradient bar (mirrors the wisp ring), header, source picker
/// (Off/Manual/Aurora), per-mode body, painted-lamps list. Wi-Fi config lives
/// under Aurora only, since that's the only mode needing internet.
class WispConfigScreen extends ConsumerWidget {
  const WispConfigScreen({super.key, required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controlAsync = ref.watch(controlNotifierProvider(lampId));
    return Scaffold(
      appBar: AppBar(
        title: const Text('Wisp'),
      ),
      body: controlAsync.when(
        loading: () => ConnectingView(deviceId: lampId),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
        ),
        data: (state) {
          return DisconnectAwareBody(
            lampId: lampId,
            child: _WispBody(lampId: lampId),
          );
        },
      ),
    );
  }
}

class _WispBody extends ConsumerStatefulWidget {
  const _WispBody({required this.lampId});
  final String lampId;

  @override
  ConsumerState<_WispBody> createState() => _WispBodyState();
}

class _WispBodyState extends ConsumerState<_WispBody> {
  /// Phone-local epoch ms at the most recent wispStatus notify. Drives the
  /// "Xs ago" indicator without trusting the wisp's own `lastSeenMs` (wisp
  /// millis, resets on reboot). Seeded on first non-empty status, refreshed
  /// by the `ref.listen` below.
  int? _lastNotifyEpochMs;

  /// 1Hz heartbeat that rebuilds the "Xs ago" label so it counts up even with
  /// no new notifies. Cancelled on dispose.
  Timer? _staleTickTimer;

  @override
  void initState() {
    super.initState();
    _staleTickTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _staleTickTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // Stamp the local clock whenever a new status lands so the "Xs ago"
    // computation has a fresh anchor. Tracking it here (not in the notifier)
    // keeps phone-local time out of the domain layer; the notifier holds only
    // what the wisp reported.
    ref.listen<AsyncValue<WispStatus>>(wispNotifierProvider(widget.lampId), (
      _,
      next,
    ) {
      if (next is AsyncData<WispStatus> && next.value.present) {
        _lastNotifyEpochMs = DateTime.now().millisecondsSinceEpoch;
      }
    });

    final async = ref.watch(wispNotifierProvider(widget.lampId));
    return async.when(
      loading: () => const _WispLoading(),
      // A read error here almost always means "this lamp has no wisp
      // characteristic"; render the no-wisp empty state so a non-wisp lamp
      // keeps the tab usable instead of dead-ending the user.
      error: (_, _) => const _NoWispEmpty(),
      data: (status) {
        // Until the lamp's wispStatus is populated (mac set -> present), don't
        // render the source picker or palette editor: the mirrored manual
        // palette from a prior session isn't what the wisp is painting.
        if (!status.present) {
          return const _NoWispEmpty();
        }
        return _buildBody(context, status);
      },
    );
  }

  Widget _buildBody(BuildContext context, WispStatus status) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    final stale = _staleSeconds(status);
    final source = status.source;
    final auroraEnabled = status.auroraDetected;

    // First time we render in Manual mode in this session, seed the
    // draft from whatever the notifier currently has as `saved` so the
    // editor doesn't open empty if the user already saved earlier in
    // the same session. Idempotent: re-seeding with the same list is
    // a no-op for the dirty check.
    if (source == WispSourceMode.manual &&
        notifier.draftManualPalette.isEmpty &&
        notifier.savedManualPalette.isNotEmpty) {
      // Schedule next frame: mutating notifier state mid-build asserts in
      // Riverpod.
      WidgetsBinding.instance.addPostFrameCallback((_) {
        notifier.resetManualPaletteDraft();
      });
    }

    // Gradient bar lives outside the ListView's padded interior so it
    // can stretch edge-to-edge. The ListView keeps its 16px gutter for
    // the rest of the content; the bar sits flush above the header.
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        PaletteGradientBar(
          sourceMode: source,
          manualPalette: notifier.draftManualPalette,
          offColor: status.offColor,
        ),
        Expanded(
          child: ListView(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 32),
            children: [
              _WispHeader(status: status, staleSeconds: stale),
              const SizedBox(height: 16),
              _SourcePicker(
                current: source,
                auroraEnabled: auroraEnabled,
                onSelect: (m) => _runWispOp(() => notifier.setSource(m)),
              ),
              if (source == WispSourceMode.off) ...[
                const SizedBox(height: 20),
                _OffColorPicker(
                  lampId: widget.lampId,
                  current: status.offColor,
                ),
              ],
              if (source == WispSourceMode.manual) ...[
                const SizedBox(height: 20),
                _ManualPaletteEditor(lampId: widget.lampId),
              ],
              if (source == WispSourceMode.aurora) ...[
                const SizedBox(height: 24),
                if (!status.auroraConnected) ...[
                  // Aurora is the selected palette source but the wisp
                  // hasn't reached Aurora over the network yet. Surface
                  // it here, alongside the WiFi row, instead of via a
                  // permanent header chip.
                  Semantics(
                    liveRegion: true,
                    label: 'Aurora not connected',
                    child: _AuroraNotConnectedNotice(
                      wifiConnected: status.wifiConnected,
                    ),
                  ),
                  const SizedBox(height: 16),
                ],
                // Wi-Fi only matters under Aurora; Off and Manual are
                // mesh-only and need no internet backend.
                _WifiConfigRow(lampId: widget.lampId, status: status),
                const SizedBox(height: 16),
                _CurrentZone(status: status),
                const SizedBox(height: 16),
                _ObservedZonesPicker(
                  status: status,
                  onPickZone: (z) => _runWispOp(() => notifier.setZone(z)),
                ),
                if (_canClearSelection(status)) ...[
                  const SizedBox(height: 16),
                  Align(
                    alignment: Alignment.centerLeft,
                    child: TextButton.icon(
                      onPressed: () => _runWispOp(notifier.clearZone),
                      icon: const Icon(Icons.close, size: 16),
                      label: const Text('Clear selection'),
                      style: TextButton.styleFrom(
                        foregroundColor: BrandColors.fogGrey,
                      ),
                    ),
                  ),
                ],
              ],
              const SizedBox(height: 24),
              const SettingsGroupHeading('Painted lamps'),
              _PaintedLampsList(lampId: widget.lampId),
            ],
          ),
        ),
      ],
    );
  }

  /// Seconds since the last notify in phone-local time, or null when no
  /// notify has been recorded yet. Used by the header to badge stale
  /// status; threshold-based "is it stale?" lives at the caller.
  int? _staleSeconds(WispStatus status) {
    if (!status.present) return null;
    if (_lastNotifyEpochMs == null) return null;
    final delta = DateTime.now().millisecondsSinceEpoch - _lastNotifyEpochMs!;
    return delta ~/ 1000;
  }

  /// Only show "Clear selection" when there's a pin to clear; clearing a
  /// `firstSeen` or `none` source is a no-op on the wisp and just confuses.
  bool _canClearSelection(WispStatus s) =>
      s.zoneSource == ZoneSource.appOp || s.zoneSource == ZoneSource.nvs;

  /// Runs a wispOp and surfaces failures as a SnackBar. Without this the
  /// notifier's optimistic update would stick forever on a failed write (no
  /// status notify comes back to reconcile it).
  Future<void> _runWispOp(Future<void> Function() op) async {
    try {
      await op();
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(context, "Couldn't reach the wisp — try again.");
    }
  }
}

/// Shown during the first `readStatus()` after opening the wisp tab. The lamp
/// is already connected; this waits only on the CHAR_WISP_STATUS round-trip.
/// Labelled "Connecting to wisp" to distinguish it from the lamp-level
/// ConnectingView.
class _WispLoading extends StatelessWidget {
  const _WispLoading();

  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          CircularProgressIndicator(color: BrandColors.fogGrey),
          SizedBox(height: 12),
          Text(
            'Connecting to wisp…',
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 13),
          ),
        ],
      ),
    );
  }
}

/// Shown when the lamp returned `WispStatus.empty` (no wispMac): no wisp heard
/// on the mesh, or the lamp lacks the wisp characteristic. No picker or
/// editor, to avoid configuring a wisp that isn't there.
class _NoWispEmpty extends StatelessWidget {
  const _NoWispEmpty();

  @override
  Widget build(BuildContext context) {
    return const EmptyStatePane(
      icon: _TwoOrbsIcon(size: 56),
      title: 'No wisp detected',
      subtitle: "This lamp hasn't heard a wisp on the mesh yet. "
          "Make sure the wisp is powered on and within range.",
    );
  }
}

/// Static two-orb glyph for the no-wisp / loading states (base + shade orbs,
/// no animation). Custom-painted because the wisp paints exactly two surfaces
/// and Material's `bubble_chart` shows three.
class _TwoOrbsIcon extends StatelessWidget {
  const _TwoOrbsIcon({required this.size});
  final double size;

  @override
  Widget build(BuildContext context) {
    return SizedBox.square(
      dimension: size,
      child: CustomPaint(
        painter: _TwoOrbsPainter(),
      ),
    );
  }
}

class _TwoOrbsPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    // Orbs arranged diagonally (upper-right shade larger than lower-left
    // base) so they read as two distinct entities. Greyscale for the empty
    // state.
    final paint = Paint()
      ..color = BrandColors.slateGrey
      ..style = PaintingStyle.fill;
    final rShade = size.width * 0.22;
    final rBase = size.width * 0.16;
    canvas.drawCircle(
      Offset(cx + size.width * 0.13, cy - size.height * 0.16),
      rShade,
      paint,
    );
    canvas.drawCircle(
      Offset(cx - size.width * 0.13, cy + size.height * 0.16),
      rBase,
      paint,
    );
  }

  @override
  bool shouldRepaint(_TwoOrbsPainter old) => false;
}

class _WispHeader extends StatelessWidget {
  const _WispHeader({required this.status, required this.staleSeconds});
  final WispStatus status;
  final int? staleSeconds;

  static const _staleThresholdSec = 60;

  @override
  Widget build(BuildContext context) {
    if (!status.present) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 16),
        child: Text(
          'No wisp detected.',
          style: TextStyle(color: BrandColors.fogGrey, fontSize: 14),
        ),
      );
    }
    final mac = status.wispMac ?? '';
    final stale = staleSeconds != null && staleSeconds! > _staleThresholdSec;
    final freshnessLabel = staleSeconds == null
        ? 'Just connected'
        : 'Last seen ${staleSeconds}s ago';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            const Icon(
              Icons.bubble_chart,
              color: BrandColors.auroraBlue,
              size: 22,
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                mac,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                  letterSpacing: 0.5,
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Text(
          freshnessLabel,
          style: TextStyle(
            color: stale ? BrandColors.error : BrandColors.fogGrey,
            fontSize: 12,
            fontStyle: stale ? FontStyle.italic : FontStyle.normal,
          ),
        ),
      ],
    );
  }
}

/// Inline notice under the Aurora pill when Aurora is selected but not yet
/// reached over the network. Points the operator at the Wi-Fi row below if
/// Wi-Fi is also down.
class _AuroraNotConnectedNotice extends StatelessWidget {
  const _AuroraNotConnectedNotice({required this.wifiConnected});
  final bool wifiConnected;

  @override
  Widget build(BuildContext context) {
    final detail = wifiConnected
        ? "Wi-Fi is up but Aurora hasn't been reached yet."
        : "The wisp isn't on Wi-Fi — configure it below.";
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: BrandColors.amberGold.withValues(alpha: 0.10),
        border: Border.all(
          color: BrandColors.amberGold.withValues(alpha: 0.40),
        ),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        children: [
          const Icon(
            Icons.auto_awesome,
            size: 16,
            color: BrandColors.amberGold,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              'Aurora not connected.\n$detail',
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 12,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _CurrentZone extends StatelessWidget {
  const _CurrentZone({required this.status});
  final WispStatus status;

  @override
  Widget build(BuildContext context) {
    final String headline;
    final String? subhead;
    // The wisp publishes a -1 sentinel for "no current zone heard yet"
    // (firmware-side equivalent of null when ZoneSelector hasn't latched).
    // Show that as a human-readable "None detected" instead of the
    // literal "Zone -1" the operator was seeing.
    final zone = status.currentZone;
    final zoneHeard = zone != null && zone >= 0;
    if (!zoneHeard) {
      headline = 'None detected';
      subhead = status.zoneSource == ZoneSource.none
          ? 'Tap a zone below to assign one — or wait for Aurora to '
              'publish one.'
          : null;
    } else {
      headline = 'Zone $zone';
      subhead = _zoneSourceLabel(status.zoneSource);
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'CURRENT ZONE',
          style: TextStyle(
            color: BrandColors.headerYellow,
            fontSize: 11,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.2,
          ),
        ),
        const SizedBox(height: 8),
        Text(
          headline,
          style: const TextStyle(
            color: BrandColors.lampWhite,
            fontSize: 24,
            fontWeight: FontWeight.w700,
          ),
        ),
        if (subhead != null) ...[
          const SizedBox(height: 4),
          Text(
            subhead,
            style: const TextStyle(
              color: BrandColors.fogGrey,
              fontSize: 12,
              fontStyle: FontStyle.italic,
            ),
          ),
        ],
      ],
    );
  }

  /// Maps the `zoneSource` enum to the parenthetical "where did this
  /// come from?" sub-label on the current-zone card.
  static String _zoneSourceLabel(ZoneSource source) {
    switch (source) {
      case ZoneSource.appOp:
        return 'Set in app';
      case ZoneSource.nvs:
        return 'Persisted on the wisp';
      case ZoneSource.firstSeen:
        return 'First zone heard on the mesh';
      case ZoneSource.none:
      case ZoneSource.unknown:
        return '';
    }
  }
}

class _ObservedZonesPicker extends StatelessWidget {
  const _ObservedZonesPicker({required this.status, required this.onPickZone});
  final WispStatus status;
  final ValueChanged<int> onPickZone;

  @override
  Widget build(BuildContext context) {
    final zones = [...status.observedZones]..sort();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'OBSERVED ZONES',
          style: TextStyle(
            color: BrandColors.headerYellow,
            fontSize: 11,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.2,
          ),
        ),
        const SizedBox(height: 8),
        if (zones.isEmpty)
          const Text(
            "No zones heard yet. Once Aurora starts publishing zone "
            "palettes on the mesh, they'll appear here.",
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
          )
        else
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final z in zones)
                _ZoneChip(
                  zoneId: z,
                  selected: z == status.currentZone,
                  onTap: () => onPickZone(z),
                ),
            ],
          ),
      ],
    );
  }
}

class _ZoneChip extends StatelessWidget {
  const _ZoneChip({
    required this.zoneId,
    required this.selected,
    required this.onTap,
  });
  final int zoneId;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final fill = selected ? BrandColors.glowPink : Colors.transparent;
    final border = selected
        ? BrandColors.glowPink
        : BrandColors.slateGrey.withValues(alpha: 0.5);
    final fg = selected ? BrandColors.midnightBlack : BrandColors.lampWhite;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        borderRadius: BorderRadius.circular(999),
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          decoration: BoxDecoration(
            color: fill,
            borderRadius: BorderRadius.circular(999),
            border: Border.all(color: border, width: selected ? 2 : 1),
          ),
          child: Text(
            'Zone $zoneId',
            style: TextStyle(
              color: fg,
              fontSize: 14,
              fontWeight: selected ? FontWeight.w700 : FontWeight.w500,
            ),
          ),
        ),
      ),
    );
  }
}

// Chunky pill picker matching the Social tab's personality picker. Aurora is
// disabled until the wisp has observed a zone, so the operator can't flip to
// Aurora and stare at a blank screen while it discovers a server.
class _SourcePicker extends StatelessWidget {
  const _SourcePicker({
    required this.current,
    required this.auroraEnabled,
    required this.onSelect,
  });

  final WispSourceMode current;
  final bool auroraEnabled;
  final ValueChanged<WispSourceMode> onSelect;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'Palette source',
          style: TextStyle(color: BrandColors.lampWhite, fontSize: 14),
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            _SourcePill(
              label: 'Off',
              icon: Icons.power_settings_new,
              selected: current == WispSourceMode.off,
              enabled: true,
              onTap: () => onSelect(WispSourceMode.off),
            ),
            const SizedBox(width: 8),
            _SourcePill(
              label: 'Manual',
              icon: Icons.palette_outlined,
              selected: current == WispSourceMode.manual,
              enabled: true,
              onTap: () => onSelect(WispSourceMode.manual),
            ),
            const SizedBox(width: 8),
            _SourcePill(
              label: 'Aurora',
              icon: Icons.auto_awesome,
              selected: current == WispSourceMode.aurora,
              enabled: auroraEnabled,
              onTap: () => onSelect(WispSourceMode.aurora),
            ),
          ],
        ),
        if (!auroraEnabled) ...[
          const SizedBox(height: 6),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 2),
            child: Text(
              "No Aurora zone heard yet. Once a zone shows up on the "
              "mesh, you'll be able to follow it.",
              style: TextStyle(
                color: BrandColors.fogGrey,
                fontSize: 11,
                fontStyle: FontStyle.italic,
              ),
            ),
          ),
        ],
      ],
    );
  }
}

class _SourcePill extends StatelessWidget {
  const _SourcePill({
    required this.label,
    required this.icon,
    required this.selected,
    required this.enabled,
    required this.onTap,
  });

  final String label;
  final IconData icon;
  final bool selected;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    // Visual contract mirrors the Social tab's _PersonalityButton:
    // selected is solid glowPink fill on midnight text; idle is slateGrey
    // outline on lampWhite. Disabled drops opacity instead of changing hue so
    // the row reads as "not for you right now" rather than "broken".
    final Color fill = enabled && selected
        ? BrandColors.glowPink
        : Colors.transparent;
    final Color border = enabled && selected
        ? BrandColors.glowPink
        : BrandColors.slateGrey.withValues(alpha: enabled ? 0.5 : 0.25);
    final Color fg = enabled && selected
        ? BrandColors.midnightBlack
        : BrandColors.lampWhite.withValues(alpha: enabled ? 1.0 : 0.4);

    return Expanded(
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: enabled ? onTap : null,
          borderRadius: BorderRadius.circular(12),
          child: Container(
            height: 72,
            decoration: BoxDecoration(
              color: fill,
              border: Border.all(
                color: border,
                width: selected && enabled ? 2 : 1,
              ),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, color: fg, size: 22),
                const SizedBox(height: 4),
                Text(
                  label,
                  style: TextStyle(
                    color: fg,
                    fontSize: 13,
                    fontWeight: selected && enabled
                        ? FontWeight.w700
                        : FontWeight.w500,
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

// Wrap of swatches with an inline Add button, matching the expression-editor
// color list. Reorder is intentionally omitted: the wisp renders the palette
// as a left->right gradient between equally-spaced stops, so swatch order
// matters visually and users rebuild rather than reorder.
class _ManualPaletteEditor extends ConsumerStatefulWidget {
  const _ManualPaletteEditor({required this.lampId});
  final String lampId;

  @override
  ConsumerState<_ManualPaletteEditor> createState() =>
      _ManualPaletteEditorState();
}

class _ManualPaletteEditorState extends ConsumerState<_ManualPaletteEditor> {
  @override
  Widget build(BuildContext context) {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // Watch so the editor rebuilds when the notifier emits draft / save
    // state changes via _bumpState.
    ref.watch(wispNotifierProvider(widget.lampId));

    if (notifier.paletteLoading) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 12),
        child: Row(
          children: [
            SizedBox(
              width: 14, height: 14,
              child: CircularProgressIndicator(
                  strokeWidth: 2, color: BrandColors.fogGrey),
            ),
            SizedBox(width: 10),
            Text('Reading palette from wisp…',
                style: TextStyle(
                    color: BrandColors.fogGrey, fontSize: 12,
                    fontStyle: FontStyle.italic)),
          ],
        ),
      );
    }

    final draft = notifier.draftManualPalette;
    final atCap = draft.length >= 10;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'MANUAL PALETTE',
          style: TextStyle(
            color: BrandColors.headerYellow,
            fontSize: 11,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.2,
          ),
        ),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            for (var i = 0; i < draft.length; i++)
              _WispColorChip(
                color: draft[i],
                onEdit: () => _editAt(i),
                onRemove: () => notifier.removeManualPaletteColor(i),
              ),
            if (!atCap)
              TextButton.icon(
                icon: const Icon(Icons.add, size: 18),
                label: const Text('Add color'),
                onPressed: _addNew,
              ),
          ],
        ),
        if (atCap) ...[
          const SizedBox(height: 4),
          const Text(
            'Palette is at the 10-color cap. Remove a swatch to add another.',
            style: TextStyle(
              color: BrandColors.fogGrey,
              fontSize: 11,
              fontStyle: FontStyle.italic,
            ),
          ),
        ],
      ],
    );
  }

  Future<void> _addNew() async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // Default new swatch: pure white. Hue-saturated would feel editorial;
    // white reads as "blank slate, pick me".
    const initial = LampColor(r: 255, g: 255, b: 255, w: 0);
    final picked = await showColorPickerSheet(
      context,
      initial: initial,
      title: 'Add palette color',
      bpp: 3, // RGB only: wisp manual palette has no W channel.
    );
    if (picked == null) return;
    notifier.appendManualPaletteColor(picked);
  }

  Future<void> _editAt(int index) async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    final draft = notifier.draftManualPalette;
    if (index < 0 || index >= draft.length) return;
    // onLive streams the picker's in-progress color into the notifier on each
    // drag tick. The notifier debounces the BLE write so the wisp isn't
    // saturated; the gradient bar updates immediately. Cancel restores the
    // original color (one trailing write).
    final original = draft[index];
    final picked = await showColorPickerSheet(
      context,
      initial: original,
      title: 'Edit palette color',
      bpp: 3,
      onLive: (live) => notifier.updateManualPaletteColor(index, live),
    );
    if (picked == null) {
      notifier.updateManualPaletteColor(index, original);
    } else {
      notifier.updateManualPaletteColor(index, picked);
    }
  }
}

/// Off-mode swatch picker. In Off mode the wisp broadcasts no palette
/// (PaintDistributor held off); the operator can still pick the color for the
/// wisp's own 30-pixel ring. The wisp persists the choice in NVS.
class _OffColorPicker extends ConsumerStatefulWidget {
  const _OffColorPicker({required this.lampId, required this.current});

  final String lampId;
  final LampColor current;

  @override
  ConsumerState<_OffColorPicker> createState() => _OffColorPickerState();
}

class _OffColorPickerState extends ConsumerState<_OffColorPicker> {
  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'WISP COLOR',
          style: TextStyle(
            color: BrandColors.headerYellow,
            fontSize: 11,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.2,
          ),
        ),
        const SizedBox(height: 8),
        const Padding(
          padding: EdgeInsets.only(bottom: 12),
          child: Text(
            "Off doesn't broadcast a palette — your lamps stay on their "
            "own behaviour. Pick the color the wisp itself shows.",
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
          ),
        ),
        Row(
          children: [
            GestureDetector(
              onTap: _pick,
              child: LampColorSwatch(color: widget.current, size: 48),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                widget.current.toHex().toUpperCase(),
                style: const TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 14,
                  fontFamily: 'monospace',
                ),
              ),
            ),
            TextButton.icon(
              onPressed: _pick,
              icon: const Icon(Icons.edit, size: 16),
              label: const Text('Change'),
            ),
          ],
        ),
      ],
    );
  }

  Future<void> _pick() async {
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    // onLive streams every drag tick into setOffColor; the notifier debounces
    // the BLE write so the wisp isn't flooded. Cancel restores the original
    // color (one trailing write).
    final original = widget.current;
    final picked = await showColorPickerSheet(
      context,
      initial: original,
      title: 'Wisp color (Off)',
      bpp: 3,
      onLive: notifier.setOffColor,
    );
    if (picked == null) {
      await notifier.setOffColor(original);
    } else {
      await notifier.setOffColor(picked);
    }
  }
}

/// Expressions-style swatch chip: tap to edit, top-right X to remove. The X is
/// always shown (the wisp's manual palette may be empty; it falls back to warm
/// white on the ring).
class _WispColorChip extends StatelessWidget {
  const _WispColorChip({
    required this.color,
    required this.onEdit,
    required this.onRemove,
  });

  final LampColor color;
  final VoidCallback onEdit;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    return Stack(
      clipBehavior: Clip.none,
      children: [
        GestureDetector(
          onTap: onEdit,
          child: LampColorSwatch(color: color, size: 40),
        ),
        Positioned(
          top: -6,
          right: -6,
          child: Semantics(
            label: 'Remove color',
            button: true,
            child: InkWell(
              onTap: onRemove,
              customBorder: const CircleBorder(),
              child: Container(
                width: 18,
                height: 18,
                decoration: const BoxDecoration(
                  color: BrandColors.ashGrey,
                  shape: BoxShape.circle,
                ),
                child: const Icon(
                  Icons.close,
                  size: 12,
                  color: BrandColors.lampWhite,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

// Tappable settings row that opens the lamp's network-picker sheet (the lamp
// owns the scan radio, the wisp doesn't). Picking a network prompts for a
// password, then ships credentials via the `setWifi` wispOp. No optimistic
// UI: a wrong password would otherwise leave a permanent "Connected" badge,
// so the subtitle echoes `WispStatus.wifiConnected`.
class _WifiConfigRow extends ConsumerStatefulWidget {
  const _WifiConfigRow({required this.lampId, required this.status});
  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<_WifiConfigRow> createState() => _WifiConfigRowState();
}

class _WifiConfigRowState extends ConsumerState<_WifiConfigRow> {
  bool _busy = false;

  @override
  Widget build(BuildContext context) {
    final subtitle = _busy
        ? 'Sending credentials to wisp…'
        : (widget.status.wifiConnected
            ? 'Connected — tap to change network'
            : 'Not connected — tap to configure');
    return SettingsRow(
      key: const Key('wifi-config-row'),
      icon: Icons.wifi,
      title: 'WiFi',
      subtitle: subtitle,
      onTap: _busy ? null : _openPicker,
    );
  }

  Future<void> _openPicker() async {
    final picked = await showWifiPickerSheet(
      context,
      lampId: widget.lampId,
    );
    if (picked == null) return;
    if (!mounted) return;
    // Prompt for the password. Open networks could skip this, but a confirm
    // step before shipping creds is still wanted, and the wisp's wifi op takes
    // a `pw` field regardless.
    final pw = await showPasswordPromptDialog(
      context,
      title: 'Password for ${picked.ssid}',
      subtitle: picked.encrypted
          ? 'Enter the WiFi password to share with the wisp.'
          : 'This network appears to be open. Leave blank or enter '
              'a password if required.',
      confirmLabel: 'Save',
    );
    if (pw == null) return;
    if (!mounted) return;

    setState(() => _busy = true);
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    try {
      await notifier.setWifi(picked.ssid, pw);
      if (!mounted) return;
      AppSnackbar.info(
        context, 'Wi-Fi creds sent to wisp (${picked.ssid}).',
      );
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(
        context, "Couldn't reach the wisp — try again.",
      );
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }
}

// Lists every inventory lamp with the two colors the wisp paints on it
// (base + shade), computed locally by re-running the wisp's
// `sampleTupleForMac` against the published palette. The wisp broadcasts no
// per-lamp roster.
//
// Accuracy caveat: on Android `InventoryLamp.id` is the BLE MAC, which differs
// from the ESP-NOW MAC by one byte, so colors follow the wisp's pattern
// (varied, ~50/50 base/shade swap) but won't byte-match the physical lamp.
// The header subtitle says to trust the lamp, not the preview.
class _PaintedLampsList extends ConsumerWidget {
  const _PaintedLampsList({required this.lampId});

  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(wispNotifierProvider(lampId).notifier);
    ref.watch(wispNotifierProvider(lampId));
    final palette = notifier.savedManualPalette;
    final inventoryAsync = ref.watch(inventoryNotifierProvider);
    return inventoryAsync.when(
      loading: () => const Padding(
        padding: EdgeInsets.symmetric(vertical: 12),
        child: Text(
          'Loading…',
          style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
        ),
      ),
      error: (_, _) => const Padding(
        padding: EdgeInsets.symmetric(vertical: 12),
        child: Text(
          "Couldn't load inventory.",
          style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
        ),
      ),
      data: (lamps) {
        if (lamps.isEmpty) {
          return const Padding(
            padding: EdgeInsets.symmetric(vertical: 12),
            child: Text(
              "No lamps in your inventory yet.",
              style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
            ),
          );
        }
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Padding(
              padding: const EdgeInsets.only(top: 4, bottom: 12),
              child: Text(
                palette.isEmpty
                    ? "The wisp hasn't published a palette yet — once it "
                        "does, this will preview each lamp's two colors."
                    : "App-side preview from the wisp's published palette. "
                        "The physical lamps are the source of truth.",
                style: const TextStyle(
                  color: BrandColors.fogGrey,
                  fontSize: 11,
                  fontStyle: FontStyle.italic,
                ),
              ),
            ),
            for (final lamp in lamps)
              _PaintedLampRow(
                lamp: lamp,
                palette: palette,
              ),
          ],
        );
      },
    );
  }
}

class _PaintedLampRow extends StatelessWidget {
  const _PaintedLampRow({required this.lamp, required this.palette});

  final InventoryLamp lamp;
  final List<LampColor> palette;

  @override
  Widget build(BuildContext context) {
    final mac = parseMacFromBleId(lamp.id);
    final prediction = (mac == null || palette.isEmpty)
        ? null
        : predictTuple(mac: mac, palette: palette);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          Expanded(
            child: Text(
              lamp.name,
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 14,
                fontWeight: FontWeight.w500,
              ),
              overflow: TextOverflow.ellipsis,
            ),
          ),
          if (prediction == null)
            const Text(
              '—',
              style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
            )
          else ...[
            // Tooltip surfaces the hex on long-press so color-blind operators
            // can distinguish base from shade without the swatch alone. RGB
            // only: the wisp's wire palette has no W channel.
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
