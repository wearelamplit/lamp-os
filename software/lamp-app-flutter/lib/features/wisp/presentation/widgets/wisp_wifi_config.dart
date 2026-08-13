import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/app_snackbar.dart';
import '../../../../core/widgets/info_panel.dart';
import '../../../../core/widgets/password_prompt_dialog.dart';
import '../../../../core/widgets/settings_row.dart';
import '../../../lamp_shell/presentation/widgets/wifi_network_picker.dart';
import '../../application/wisp_notifier.dart';
import '../../domain/wisp_status.dart';

// Tappable settings row for WiFi config. The lamp owns the scan radio;
// picking a network prompts for a password, then ships via `setWifi` wispOp.
// No optimistic UI: a wrong password or out-of-range AP would leave a
// sticky "Connected" badge with nothing to reconcile it.
class WifiConfigRow extends ConsumerStatefulWidget {
  const WifiConfigRow({super.key, required this.lampId, required this.status});
  final String lampId;
  final WispStatus status;

  @override
  ConsumerState<WifiConfigRow> createState() => _WifiConfigRowState();
}

class _WifiConfigRowState extends ConsumerState<WifiConfigRow> {
  bool _busy = false;

  @override
  Widget build(BuildContext context) {
    final mismatch = widget.status.wifiChannelMismatch;
    final subtitle = _busy
        ? 'Sending credentials to wisp…'
        : mismatch
            ? 'Wi-Fi channel conflict. Tap to change network.'
            : (widget.status.wifiConnected
                ? 'Connected. Tap to change network.'
                : 'Not connected. Tap to configure.');
    final row = SettingsRow(
      key: const Key('wifi-config-row'),
      icon: Icons.wifi,
      title: 'WiFi',
      subtitle: subtitle,
      onTap: _busy ? null : _openPicker,
    );
    if (!mismatch) return row;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        row,
        _ChannelMismatchPanel(
          apChannel: widget.status.wifiApChannel,
          onRetry: _busy ? null : _retryReconnect,
        ),
      ],
    );
  }

  Future<void> _retryReconnect() async {
    setState(() => _busy = true);
    final notifier = ref.read(wispNotifierProvider(widget.lampId).notifier);
    try {
      await notifier.wifiReconnect();
      if (!mounted) return;
      AppSnackbar.info(context, 'Asked the wisp to reconnect to Wi-Fi.');
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.error(context, "Couldn't reach the wisp. Try again.");
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _openPicker() async {
    final picked = await showWifiPickerSheet(
      context,
      lampId: widget.lampId,
    );
    if (picked == null) return;
    if (!mounted) return;
    // Always prompt for a password; the wispOp takes a `pw` field
    // regardless, and a confirm step prevents accidental credential sends.
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
        context, "Couldn't reach the wisp. Try again.",
      );
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }
}

// Explain-and-fix callout shown when the wisp rejected the home AP for being
// off channel 6. Channel 6 is a permanent fleet constant; only the offending
// channel comes from the wisp.
class _ChannelMismatchPanel extends StatelessWidget {
  const _ChannelMismatchPanel({required this.apChannel, required this.onRetry});

  final int apChannel;
  final VoidCallback? onRetry;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    final routerClause = apChannel > 0
        ? 'Your router is on channel $apChannel'
        : 'Your router is on the wrong channel';
    final body = '$routerClause, but the lamp network runs on channel 6. '
        "The wisp has a single radio and can't be on both at once, so it "
        'stayed on the lamp network instead of your Wi-Fi. To fix: set your '
        'router\'s 2.4 GHz band to channel 6 (not "Auto"), then tap Retry. '
        'Once your router is on channel 6, the wisp can use Wi-Fi and the '
        'lamps at the same time.';
    return InfoPanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Wi-Fi channel conflict',
            style: textTheme.titleSmall?.copyWith(
              color: colorScheme.onSurface,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: AppSpace.sm),
          Text(body),
          const SizedBox(height: AppSpace.sm),
          Align(
            alignment: Alignment.centerLeft,
            child: TextButton.icon(
              key: const Key('wifi-channel-mismatch-retry'),
              onPressed: onRetry,
              icon: const Icon(
                Icons.refresh,
                size: 16, // fixed icon dimension
              ),
              label: const Text('Retry'),
              style: TextButton.styleFrom(
                foregroundColor: colorScheme.tertiary,
                padding: EdgeInsets.zero,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
