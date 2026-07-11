import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/widgets/app_snackbar.dart';
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
    final subtitle = _busy
        ? 'Sending credentials to wisp…'
        : (widget.status.wifiConnected
            ? 'Connected. Tap to change network.'
            : 'Not connected. Tap to configure.');
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
