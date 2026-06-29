import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/info_panel.dart';
import '../../control/application/control_notifier.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import 'widgets/wifi_network_picker.dart';

/// Home Mode pane — presence-only detection of a home WiFi network.
///
/// The lamp never associates to the AP. It periodically scans for nearby
/// SSIDs (background scans gated to BT-disconnected windows so they don't
/// stress the shared radio) and treats the user's selected SSID being
/// visible as "I'm at home". No password is stored or transmitted, which
/// also closes off the spoofed-AP password-capture attack.
///
/// While the user is on this page, the firmware unconditionally treats
/// home mode as ACTIVE via the CHAR_HOME_MODE_FOCUS signal — so the
/// brightness slider previews the home brightness in real time. When
/// the user leaves the page, the focus signal clears and the firmware
/// falls back to "configurator" mode (home mode off) for the rest of
/// the BT session.
class HomeModeScreen extends ConsumerStatefulWidget {
  const HomeModeScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<HomeModeScreen> createState() => _HomeModeScreenState();
}

class _HomeModeScreenState extends ConsumerState<HomeModeScreen> {
  // Captured in initState so dispose can fire the focus-off write WITHOUT
  // touching `ref` after super.dispose() — the previous pattern of
  // ref.read(...) in dispose was tripping
  // `_lifecycleState != _ElementLifecycle.defunct` framework asserts
  // because the Element is mid-teardown by then.
  late BleClient _ble;

  @override
  void initState() {
    super.initState();
    _ble = ref.read(bleClientProvider);
    // Tell the lamp "user is on the Home Mode page": forces home-mode
    // gate ON so the brightness slider previews home brightness live,
    // and routes incoming CHAR_BRIGHTNESS writes to homeMode.brightness.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _writeFocus(true);
    });
  }

  @override
  void dispose() {
    // Tell the lamp "user is leaving" — fire-and-forget via the captured
    // BleClient. The firmware also clears the flag on BT disconnect as
    // a safety net, so a missed exit (app killed mid-page) is recovered.
    _writeFocus(false);
    super.dispose();
  }

  void _writeFocus(bool active) {
    unawaited(_ble.write(
      widget.lampId,
      BleUuids.controlService,
      BleUuids.homeModeFocus,
      Uint8List.fromList([active ? 1 : 0]),
      withoutResponse: true,
    ).catchError((_) {
      // best-effort — BT may already be torn down by route pop
    }));
  }

  void _selectSsid(String ssid) {
    // Draft-only: hold the SSID in controlNotifier state and let global
    // Save Changes persist it via settings_blob (the firmware merges
    // homeMode.ssid on blob writes). No live firmware effect to preview
    // for an SSID — home-mode detection runs off the saved value.
    ref.read(controlNotifierProvider(widget.lampId).notifier)
        .setHomeSsid(ssid);
  }

  void _onForget() {
    // Same draft-only flow: clear the SSID in app state; Save Changes
    // persists. Users who want the lamp to drop home mode *immediately*
    // can hit Save Changes after Forget.
    ref.read(controlNotifierProvider(widget.lampId).notifier)
        .setHomeSsid('');
  }

  @override
  Widget build(BuildContext context) {
    // .select to just the home section. HomeMode reads only
    // home.{ssid, brightness}; brightness sliders on the active lamp's
    // ControlNotifier don't rebuild this whole tree.
    final controlAsync = ref.watch(controlNotifierProvider(widget.lampId)
        .select((a) => a.whenData((s) => s.home)));

    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => GoRouter.maybeOf(context)?.pop(),
        ),
        title: const Text('Home Mode'),
      ),
      body: controlAsync.when(
        loading: () => ConnectingView(deviceId: widget.lampId),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () =>
              ref.invalidate(controlNotifierProvider(widget.lampId)),
        ),
        data: (home) {
          final notifier =
              ref.read(controlNotifierProvider(widget.lampId).notifier);

          final hasSaved = home.ssid.isNotEmpty;

          return DisconnectAwareBody(
            lampId: widget.lampId,
            child: ListView(
            padding: const EdgeInsets.all(16),
            children: [
              const InfoPanel(
                child: Text(
                  'Home Mode quiets the lamp for everyday use — when the '
                  'lamp sees your home Wi-Fi nearby it pauses social '
                  'greetings and switches to a calmer brightness. The '
                  "lamp doesn't connect to the network or store its "
                  'password — it just listens for the name in the air.',
                ),
              ),
              const SizedBox(height: 16),

              if (hasSaved) ...[
                _ConnectionStatusRow(
                  label: 'Home network: ${home.ssid}',
                  onForget: _onForget,
                ),
                const SizedBox(height: 12),
              ] else ...[
                WifiNetworkPicker(
                  lampId: widget.lampId,
                  onPick: (r) => _selectSsid(r.ssid),
                ),
              ],

              // Home brightness — slider always available; the firmware
              // routes CHAR_BRIGHTNESS to home.brightness while this page
              // is open (via CHAR_HOME_MODE_FOCUS), so dragging the
              // slider previews the home brightness live on the lamp.
              const SizedBox(height: 24),
              const Text(
                'Home Mode brightness',
                style: TextStyle(
                  color: BrandColors.headerYellow,
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.8,
                ),
              ),
              Row(
                children: [
                  Expanded(
                    child: Slider(
                      value: home.brightness.toDouble(),
                      min: 0,
                      max: 100,
                      divisions: 100,
                      onChanged: (v) =>
                          notifier.setHomeBrightness(v.round()),
                      onChangeEnd: (v) =>
                          notifier.setHomeBrightness(v.round()),
                    ),
                  ),
                  SizedBox(
                    width: 48,
                    child: Text(
                      '${home.brightness}%',
                      textAlign: TextAlign.right,
                      style: const TextStyle(
                        color: BrandColors.fogGrey,
                        fontSize: 12,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                ],
              ),
            ],
          ),
          );
        },
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Private widgets
// ---------------------------------------------------------------------------

class _ConnectionStatusRow extends StatelessWidget {
  const _ConnectionStatusRow({
    required this.label,
    required this.onForget,
  });
  final String label;
  final VoidCallback onForget;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: Text(label,
              style: const TextStyle(color: BrandColors.lampWhite)),
        ),
        TextButton.icon(
          icon: const Icon(Icons.wifi_off, size: 16),
          label: const Text('Forget network'),
          onPressed: onForget,
        ),
      ],
    );
  }
}

