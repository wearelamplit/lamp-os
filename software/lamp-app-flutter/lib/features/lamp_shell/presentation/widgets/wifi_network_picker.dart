import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/brand_extras.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../application/wifi_notifier.dart';
import '../../domain/wifi_state.dart';

/// Inline WiFi-network list — shows scan results, a refresh button, and a
/// row-tap callback. Used by Home Mode (inline in the page) and the Wisp
/// pane (inside a bottom sheet). The lamp owns the scan radio so both
/// surfaces share the same [wifiNotifierProvider].
///
/// The widget kicks off an initial scan on first mount if the scan list
/// is empty (so the user doesn't see a "tap refresh" prompt the first
/// time they land on a surface that just opened it). Subsequent visits
/// reuse the cached list; the operator can tap refresh to rescan.
///
/// `currentSsid` — if non-null, the matching row renders with a check
/// icon to signal "this is what's currently set". The row is still
/// tappable so the user can re-pick it (idempotent on the wisp side).
class WifiNetworkPicker extends ConsumerStatefulWidget {
  const WifiNetworkPicker({
    super.key,
    required this.lampId,
    required this.onPick,
    this.currentSsid,
    this.emptyHint,
  });

  final String lampId;
  final ValueChanged<WifiScanResult> onPick;
  final String? currentSsid;
  final String? emptyHint;

  @override
  ConsumerState<WifiNetworkPicker> createState() => _WifiNetworkPickerState();
}

class _WifiNetworkPickerState extends ConsumerState<WifiNetworkPicker> {
  bool _didKickoffScan = false;

  @override
  Widget build(BuildContext context) {
    final wifiAsync = ref.watch(wifiNotifierProvider(widget.lampId));
    final wifiNotifier =
        ref.read(wifiNotifierProvider(widget.lampId).notifier);
    final wifi = wifiAsync.value ?? const WifiState();
    final isScanning = wifi.state == 'scanning';

    // Kick off a scan on first paint if we have nothing to show. Mirrors
    // the home-mode pattern of "land on the page → see networks without
    // pressing anything". Subsequent visits reuse the cached list.
    if (!_didKickoffScan && wifi.scanResults.isEmpty && !isScanning) {
      _didKickoffScan = true;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) wifiNotifier.scan();
      });
    }

    final colorScheme = Theme.of(context).colorScheme;
    final extras = context.brandExtras;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                'Pick a network',
                style: TextStyle(
                  color: colorScheme.onSurfaceVariant,
                  fontWeight: FontWeight.w600,
                  fontSize: 13,
                  letterSpacing: 0.5,
                ),
              ),
            ),
            SizedBox(
              width: 36,
              height: 36,
              child: isScanning
                  ? const Padding(
                      padding: EdgeInsets.all(8),
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : IconButton(
                      key: const Key('wifi-picker-refresh'),
                      icon: const Icon(Icons.refresh, size: 20),
                      tooltip: 'Scan for networks',
                      onPressed: isScanning ? null : wifiNotifier.scan,
                      padding: EdgeInsets.zero,
                    ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        if (wifi.scanResults.isEmpty && !isScanning)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 16),
            child: Text(
              widget.emptyHint ?? 'Tap refresh to scan for networks.',
              style: TextStyle(color: colorScheme.onSurfaceVariant),
              textAlign: TextAlign.center,
            ),
          ),
        ...wifi.scanResults.map((r) {
          final isCurrent = widget.currentSsid != null &&
              r.ssid == widget.currentSsid;
          return ListTile(
            key: Key('wifi-picker-row-${r.ssid}'),
            contentPadding: const EdgeInsets.symmetric(horizontal: 0),
            leading: RssiBars(rssi: r.rssi),
            title: Text(r.ssid),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (r.encrypted)
                  Icon(
                    Icons.lock_outline,
                    size: 16,
                    color: colorScheme.onSurfaceVariant,
                  ),
                if (isCurrent) ...[
                  const SizedBox(width: 6),
                  Icon(
                    Icons.check,
                    size: 16,
                    color: extras.success,
                  ),
                ],
              ],
            ),
            onTap: () => widget.onPick(r),
          );
        }),
      ],
    );
  }
}

/// 4-bar RSSI indicator. Public so other surfaces (e.g. a future "all
/// observed networks" debug screen) can render the same iconography
/// without re-deriving the bar-count thresholds.
class RssiBars extends StatelessWidget {
  const RssiBars({super.key, required this.rssi});
  final int rssi;

  int get _bars => rssi >= -55
      ? 4
      : rssi >= -65
          ? 3
          : rssi >= -75
              ? 2
              : 1;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 22,
      height: 22,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        mainAxisSize: MainAxisSize.min,
        children: List.generate(4, (i) {
          final filled = i < _bars;
          final extras = context.brandExtras;
          final colorScheme = Theme.of(context).colorScheme;
          return Container(
            width: 3,
            height: 6.0 + i * 4,
            margin: const EdgeInsets.symmetric(horizontal: 1),
            decoration: BoxDecoration(
              color: filled
                  ? extras.success
                  : colorScheme.onSurfaceVariant.withValues(alpha: 0.35),
              borderRadius: BorderRadius.circular(1),
            ),
          );
        }),
      ),
    );
  }
}

/// Modal bottom-sheet variant of [WifiNetworkPicker]. Returns the picked
/// scan result on tap, or null if the user dismisses without picking.
/// Used by the Wisp pane's WiFi tile.
Future<WifiScanResult?> showWifiPickerSheet(
  BuildContext context, {
  required String lampId,
  String? currentSsid,
}) {
  return showAppSheet<WifiScanResult>(
    context,
    builder: (ctx) => DraggableScrollableSheet(
      initialChildSize: 0.6,
      minChildSize: 0.4,
      maxChildSize: 0.9,
      expand: false,
      builder: (sheetCtx, scrollController) {
        final sheetColorScheme = Theme.of(sheetCtx).colorScheme;
        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Drag handle for affordance.
              Center(
                child: Container(
                  width: 40,
                  height: 4,
                  margin: const EdgeInsets.only(bottom: 12),
                  decoration: BoxDecoration(
                    color: sheetColorScheme.onSurfaceVariant.withValues(alpha: 0.5),
                    borderRadius: BorderRadius.circular(2),
                  ),
                ),
              ),
              Text(
                'WiFi networks',
                style: TextStyle(
                  color: sheetColorScheme.onSurface,
                  fontSize: 18,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(height: 8),
              Expanded(
                child: SingleChildScrollView(
                  controller: scrollController,
                  child: WifiNetworkPicker(
                    lampId: lampId,
                    currentSsid: currentSsid,
                    onPick: (r) => Navigator.of(ctx).pop(r),
                  ),
                ),
              ),
            ],
          ),
        );
      },
    ),
  );
}
