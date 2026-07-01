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
/// The wisp is a separate ESP32-C6 node; the app talks to it through
/// the relay lamp's BLE control service which proxies wispOps onto the
/// mesh and caches the wisp's status broadcasts. [lampId] is the BLE
/// proxy lamp.
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
    // Phone-local timestamp for "Xs ago" freshness; keeping it here
    // avoids leaking wall-clock time into the domain layer.
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
      // Read error likely means this lamp has no wisp characteristic;
      // render the no-wisp empty state.
      error: (_, _) => const _NoWispEmpty(),
      data: (status) {
        // Guard: absent wisp would show the prior session's local palette.
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

    // Seed the draft from saved on first Manual render; idempotent if
    // already in sync (re-seeding the same list is a no-op for dirty check).
    if (source == WispSourceMode.manual &&
        notifier.draftManualPalette.isEmpty &&
        notifier.savedManualPalette.isNotEmpty) {
      // Schedule on the next frame to avoid mutating notifier state
      // mid-build (Riverpod asserts).
      WidgetsBinding.instance.addPostFrameCallback((_) {
        notifier.resetManualPaletteDraft();
      });
    }

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
                  Semantics(
                    liveRegion: true,
                    label: 'Aurora not connected',
                    child: AuroraNotConnectedNotice(
                      wifiConnected: status.wifiConnected,
                    ),
                  ),
                  const SizedBox(height: AppSpace.lg),
                ],
                // Wi-Fi only needed under Aurora.
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
  /// notify has arrived yet.
  int? _staleSeconds(WispStatus status) {
    if (!status.present) return null;
    if (_lastNotifyEpochMs == null) return null;
    final delta = DateTime.now().millisecondsSinceEpoch - _lastNotifyEpochMs!;
    return delta ~/ 1000;
  }

  /// Only true when the user has an explicit zone pin (appOp or nvs);
  /// clearing a firstSeen/none source is a no-op on the wisp side.
  bool _canClearSelection(WispStatus s) =>
      s.zoneSource == ZoneSource.appOp || s.zoneSource == ZoneSource.nvs;

  /// Surfaces wispOp errors as a SnackBar. Failed writes have no
  /// incoming notify to reconcile the optimistic state.
  Future<void> _runWispOp(Future<void> Function() op) async {
    try {
      await op();
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(context, "Couldn't reach the wisp — try again.");
    }
  }
}

/// Loading state during the initial `CHAR_WISP_STATUS` BLE round-trip.
/// "Connecting to wisp" wording distinguishes this from the earlier
/// lamp-connect phase the user already passed.
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

/// Shown when the lamp returned [WispStatus.empty] (no wispMac): no
/// wisp heard on the mesh from this lamp, or the lamp predates the
/// wisp characteristic.
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

/// Static two-orb glyph for the no-wisp / loading affordances.
/// Two orbs: the wisp paints exactly two surfaces (base + shade).
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
    // Shade orb larger than base so they read as two distinct entities.
    // Greyscale per empty-state visual language.
    final paint = Paint()
      ..color = color
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
  bool shouldRepaint(_TwoOrbsPainter old) => old.color != color;
}
