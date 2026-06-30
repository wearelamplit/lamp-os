import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../control/application/control_notifier.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import '../application/wisp_notifier.dart';
import '../domain/wisp_source_mode.dart';
import '../domain/wisp_status.dart';
import '../domain/zone_source.dart';
import 'palette_gradient_bar.dart';
import 'widgets/wisp_header.dart';
import 'widgets/wisp_manual_palette.dart';
import 'widgets/wisp_off_color.dart';
import 'widgets/wisp_painted_lamps.dart';
import 'widgets/wisp_source_picker.dart';
import 'widgets/wisp_wifi_config.dart';
import 'widgets/wisp_zones.dart';

/// Wisp config — controls how the wisp drives the lamp grid's paint.
///
/// Pushed from the WispIndicator's 5-tap-orbs gesture on a wisp-painted
/// lamp. The bottom-nav Wisp tab is gone; the orbs are the only entry
/// point (same gesture style as the Lamplit-wordmark advanced-unlock).
///
/// The wisp is a separate ESP32-C6 node that can either follow an Aurora
/// zone (subscription palette) or repaint the mesh from an operator-
/// defined palette. From the lamp app's perspective it's an opaque peer;
/// we talk to it through the lamp's BLE control service which proxies
/// wispOps onto the mesh and caches the wisp's status broadcasts back
/// at us. The lamp parameter is the BLE proxy — same lamp the user is
/// connected to in LampShell.
///
/// Layout (top-down):
///   0. Palette gradient bar — full-width, no padding, mirrors the wisp's
///      30-pixel NeoPixel ring. Off/Aurora-without-palette fall back to
///      warm-white; Manual previews the editor's current draft live.
///   1. Header — wisp MAC + last-seen freshness
///   2. Source pill picker — Off / Manual / Aurora (Aurora disabled
///      until at least one zone has been observed)
///   3. Per-mode body:
///       Off    → off-color picker
///       Manual → palette editor (up to 10 colors)
///       Aurora → not-connected notice (when applicable), Wi-Fi config,
///                current-zone callout, observed-zones picker, clear-
///                selection button. Wi-Fi lives here — and only here —
///                because the wisp only needs internet under Aurora.
///   4. Lamps — all lamps the wisp claims by bdAddr, with a two-color
///      preview for each + a shuffle control. Names resolve from the
///      connected lamp's nearby-peer list; unresolved show a short tail.
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
  /// Phone-local epoch ms at the most recent wispStatus notify. Used to
  /// derive a "Xs ago" indicator without trusting the wisp's own
  /// `lastSeenMs` (which is wisp millis, resets on wisp reboot, and is
  /// useless for the human-time "is the wisp still alive?" question).
  ///
  /// Seeded eagerly on first non-empty status; refreshed by the
  /// `ref.listen` below on every subsequent state change.
  int? _lastNotifyEpochMs;

  /// 1Hz heartbeat that rebuilds the "Xs ago" label so it counts up
  /// even while no new notifies are arriving. Cancelled on dispose.
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
    // computation has a fresh anchor. Tracking it here (rather than in
    // the notifier) keeps the phone-local time concern out of the
    // domain layer — the notifier holds only what the wisp reported.
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
      // A read error almost always means this lamp has no wisp
      // characteristic. Rendering the no-wisp empty state keeps the tab
      // usable and surfaces "No wisp detected" guidance.
      error: (_, _) => const _NoWispEmpty(),
      data: (status) {
        // Gate on present: without it, an absent wisp would show the
        // locally-mirrored manual palette from a prior session, which
        // isn't what the wisp is actually painting.
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
      // Schedule on the next frame so we don't mutate notifier state
      // mid-build (Riverpod would assert).
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
            padding: const EdgeInsets.fromLTRB(AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xxl),
            children: [
              WispHeader(status: status, staleSeconds: stale),
              const SizedBox(height: AppSpace.lg),
              SourcePicker(
                current: source,
                auroraEnabled: auroraEnabled,
                onSelect: (m) => _runWispOp(() => notifier.setSource(m)),
              ),
              if (source == WispSourceMode.off) ...[
                const SizedBox(height: 20),
                OffColorPicker(
                  lampId: widget.lampId,
                  current: status.offColor,
                ),
              ],
              if (source == WispSourceMode.manual) ...[
                const SizedBox(height: 20),
                ManualPaletteEditor(lampId: widget.lampId),
              ],
              if (source == WispSourceMode.aurora) ...[
                const SizedBox(height: AppSpace.xl),
                if (!status.auroraConnected) ...[
                  // Aurora is the selected palette source but the wisp
                  // hasn't reached Aurora over the network yet. Surface
                  // it here, alongside the WiFi row, instead of via a
                  // permanent header chip.
                  Semantics(
                    liveRegion: true,
                    label: 'Aurora not connected',
                    child: AuroraNotConnectedNotice(
                      wifiConnected: status.wifiConnected,
                    ),
                  ),
                  const SizedBox(height: AppSpace.lg),
                ],
                // Wi-Fi only matters under Aurora — Off and Manual modes
                // are mesh-only and don't need an internet backend.
                WifiConfigRow(lampId: widget.lampId, status: status),
                const SizedBox(height: AppSpace.lg),
                CurrentZone(status: status),
                const SizedBox(height: AppSpace.lg),
                ObservedZonesPicker(
                  status: status,
                  onPickZone: (z) => _runWispOp(() => notifier.setZone(z)),
                ),
                if (_canClearSelection(status)) ...[
                  const SizedBox(height: AppSpace.lg),
                  Align(
                    alignment: Alignment.centerLeft,
                    child: TextButton.icon(
                      onPressed: () => _runWispOp(notifier.clearZone),
                      icon: const Icon(Icons.close, size: 16),
                      label: const Text('Clear selection'),
                      style: TextButton.styleFrom(
                        foregroundColor: Theme.of(context).colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ),
                ],
              ],
              const SizedBox(height: AppSpace.xl),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 24, 16, AppSpace.sm),
                child: Row(
                  children: [
                    Text(
                      'LAMPS',
                      style: Theme.of(context).textTheme.labelLarge?.copyWith(
                            color: Theme.of(context).colorScheme.secondary,
                            fontWeight: FontWeight.w700,
                          ),
                    ),
                    const Spacer(),
                    IconButton(
                      icon: Icon(Icons.shuffle,
                          size: 18,
                          color: Theme.of(context).colorScheme.secondary),
                      visualDensity: VisualDensity.compact,
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(),
                      tooltip: 'Shuffle colours',
                      onPressed: () => ref
                          .read(wispNotifierProvider(widget.lampId).notifier)
                          .shuffle(),
                    ),
                  ],
                ),
              ),
              PaintedLampsList(lampId: widget.lampId),
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

  /// Only show the "Clear selection" button when the operator actually
  /// has a pin to clear — clearing a `firstSeen` or `none` source is a
  /// no-op on the wisp side and would just confuse the UI.
  bool _canClearSelection(WispStatus s) =>
      s.zoneSource == ZoneSource.appOp || s.zoneSource == ZoneSource.nvs;

  /// Runs a wispOp (setZone/clearZone) and surfaces failures as a
  /// SnackBar. Without this the notifier's optimistic update would
  /// stick around forever on a failed write — no status notify is
  /// coming back to reconcile it.
  Future<void> _runWispOp(Future<void> Function() op) async {
    try {
      await op();
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(context, "Couldn't reach the wisp — try again.");
    }
  }
}

/// Loading state shown while the wisp tab is doing its first
/// `readStatus()` after the user opens it. The control screen has
/// already confirmed the lamp is connected (we're past
/// `controlAsync.when`); what we're waiting on here is purely the
/// BLE round-trip for `CHAR_WISP_STATUS`. Tagged "Connecting to wisp"
/// rather than "Loading" so the user can tell apart "still hearing
/// from the lamp" (which would have shown a different ConnectingView
/// outside this widget) from "lamp is connected, asking it about the
/// wisp."
class _WispLoading extends StatelessWidget {
  const _WispLoading();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const CircularProgressIndicator(),
          const SizedBox(height: AppSpace.md),
          Text(
            'Connecting to wisp…',
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ),
    );
  }
}

/// Shown when the lamp returned `WispStatus.empty` (no wispMac
/// populated) — either no wisp has been heard on the mesh from this
/// lamp's perspective, or the lamp itself doesn't carry the wisp
/// characteristic (legacy / standalone deployment). No source picker,
/// no editor — those would only let the user accidentally configure
/// a wisp they're not actually talking to.
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

/// Static two-orb glyph for the no-wisp / loading affordances. Mirrors
/// the live [WispIndicator] (base + shade orbs) without animation, so
/// the empty state reads as "the thing that would be here." Material's
/// nearest off-the-shelf option was `bubble_chart` — three circles —
/// which broke the metaphor; the wisp paints exactly two surfaces, so
/// two orbs it is.
class _TwoOrbsIcon extends StatelessWidget {
  const _TwoOrbsIcon({required this.size});
  final double size;

  @override
  Widget build(BuildContext context) {
    return SizedBox.square(
      dimension: size,
      child: CustomPaint(
        painter: _TwoOrbsPainter(Theme.of(context).colorScheme.onSurfaceVariant),
      ),
    );
  }
}

class _TwoOrbsPainter extends CustomPainter {
  const _TwoOrbsPainter(this.color);
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final cy = size.height / 2;
    // Mid-drift snapshot of the live WispIndicator: orbs arranged
    // diagonally (upper-right + lower-left) rather than vertically
    // stacked, with the shade orb visibly larger than the base orb so
    // they read as two distinct entities at a glance. Greyscale per
    // empty-state visual language — the live indicator uses the wisp's
    // actual paint colors here.
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;
    final rShade = size.width * 0.22;
    final rBase = size.width * 0.16;
    // Upper-right: bigger (shade)
    canvas.drawCircle(
      Offset(cx + size.width * 0.13, cy - size.height * 0.16),
      rShade,
      paint,
    );
    // Lower-left: smaller (base)
    canvas.drawCircle(
      Offset(cx - size.width * 0.13, cy + size.height * 0.16),
      rBase,
      paint,
    );
  }

  @override
  bool shouldRepaint(_TwoOrbsPainter old) => old.color != color;
}
