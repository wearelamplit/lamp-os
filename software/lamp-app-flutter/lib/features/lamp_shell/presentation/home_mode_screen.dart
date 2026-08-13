import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/form_section.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/info_panel.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/domain/sections.dart';
import 'widgets/wifi_network_picker.dart';

/// Home Mode pane. Quiets the lamp for everyday use.
///
/// When `networkBound` is on, home mode is presence-driven: the lamp
/// periodically scans for the saved SSID and activates when it's visible
/// (background scans gated to BT-disconnected windows). When `networkBound`
/// is off, home mode is a plain manual on/off (no WiFi scanning).
///
/// While the user is on this page, the firmware unconditionally treats
/// home mode as ACTIVE via the CHAR_HOME_MODE_FOCUS signal, so the
/// brightness slider previews the home brightness in real time. When
/// the user leaves the page, the focus signal clears and the firmware
/// falls back to "configurator" mode for the rest of the BT session.
class HomeModeScreen extends ConsumerStatefulWidget {
  const HomeModeScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<HomeModeScreen> createState() => _HomeModeScreenState();
}

class _HomeModeScreenState extends ConsumerState<HomeModeScreen> {
  // Captured in initState so dispose can fire the focus-off write WITHOUT
  // touching `ref` after super.dispose(). ref.read(...) in dispose trips
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
    // Tell the lamp "user is leaving": fire-and-forget via the captured
    // BleClient. The firmware also clears the flag on BT disconnect, so
    // a missed exit (app killed mid-page) is recovered.
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
      // BT may already be torn down by route pop
    }));
  }

  @override
  Widget build(BuildContext context) {
    final controlAsync = ref.watch(controlNotifierProvider(widget.lampId));

    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => GoRouter.maybeOf(context)?.pop(),
        ),
        title: const Text('Home Mode'),
      ),
      body: controlAsync.when(
        loading: () => const SizedBox.expand(),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () =>
              ref.invalidate(controlNotifierProvider(widget.lampId)),
        ),
        data: (state) => _HomeModeBody(lampId: widget.lampId, state: state),
      ),
    );
  }
}

class _HomeModeBody extends ConsumerWidget {
  const _HomeModeBody({required this.lampId, required this.state});
  final String lampId;
  final ControlState state;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final home = state.home;
    final notifier = ref.read(controlNotifierProvider(lampId).notifier);
    final catalog = state.catalog;

    return ListView(
      padding: const EdgeInsets.all(AppSpace.lg),
      children: [
        Row(
          children: [
            Expanded(
              child: Text('Home Mode',
                  style: Theme.of(context).textTheme.titleMedium),
            ),
            Switch(
              value: home.enabled,
              onChanged: (v) => notifier.setHomeEnabled(v),
            ),
          ],
        ),
        const SizedBox(height: AppSpace.lg),
        const InfoPanel(
          child: Text(
            'Home Mode quiets the lamp for everyday use. It can activate '
            'automatically when your home Wi-Fi is nearby, or simply stay '
            'on as a manual setting. The lamp never connects to the network '
            'or stores a password. It just listens for the name in the air.',
          ),
        ),
        const SizedBox(height: AppSpace.lg),

        // Network binding
        FormSection(
          title: 'Activation',
          children: [
            SettingsRow(
              icon: Icons.wifi_find_outlined,
              title: 'Only on my home network',
              subtitle: home.networkBound
                  ? 'Activates when your Wi-Fi is in range'
                  : 'Always on when Home Mode is enabled',
              trailing: Switch(
                value: home.networkBound,
                onChanged: (v) => notifier.setHomeNetworkBound(v),
              ),
            ),
          ],
        ),
        const SizedBox(height: AppSpace.lg),

        // Network picker + brightness only shown when presence-driven
        if (home.networkBound) ...[
          _NetworkSection(lampId: lampId, home: home, notifier: notifier),
          const SizedBox(height: AppSpace.lg),
        ],

        // Behaviour toggles
        FormSection(
          title: 'While home mode is active',
          children: [
            SettingsRow(
              icon: Icons.waving_hand_outlined,
              title: 'Social behaviours',
              subtitle: 'Greetings and nearby-lamp reactions',
              trailing: Switch(
                value: !home.socialDisabled,
                onChanged: (v) => notifier.setHomeSocialDisabled(!v),
              ),
            ),
          ],
        ),
        const SizedBox(height: AppSpace.lg),

        // Per-expression type toggles shown only when the catalog is available
        if (catalog != null && catalog.expressions.isNotEmpty) ...[
          FormSection(
            title: 'Expressions',
            children: [
              for (final descriptor in catalog.expressions)
                SettingsRow(
                  icon: Icons.auto_awesome_outlined,
                  title: descriptor.name,
                  trailing: Switch(
                    value:
                        !home.disabledExpressionTypes.contains(descriptor.id),
                    onChanged: (v) => notifier.setHomeExpressionDisabled(
                        descriptor.id, !v),
                  ),
                ),
            ],
          ),
          const SizedBox(height: AppSpace.lg),
        ],
      ],
    );
  }
}

class _NetworkSection extends ConsumerWidget {
  const _NetworkSection({
    required this.lampId,
    required this.home,
    required this.notifier,
  });
  final String lampId;
  final HomeSection home;
  final ControlNotifier notifier;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final hasSaved = home.ssid.isNotEmpty;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        FormSection(
          title: 'Home network',
          children: [
            if (hasSaved)
              _ConnectionStatusRow(
                label: home.ssid,
                onForget: () => notifier.setHomeSsid(''),
              )
            else
              Padding(
                padding: const EdgeInsets.all(AppSpace.lg),
                child: WifiNetworkPicker(
                  lampId: lampId,
                  onPick: (r) => notifier.setHomeSsid(r.ssid),
                ),
              ),
          ],
        ),
        const SizedBox(height: AppSpace.lg),
        FormSection(
          title: 'Home brightness',
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(
                  horizontal: AppSpace.lg, vertical: AppSpace.sm),
              child: Row(
                children: [
                  Expanded(
                    child: Slider(
                      value: home.brightness.toDouble(),
                      min: 0,
                      max: 100,
                      divisions: 100,
                      onChanged: (v) => notifier.setHomeBrightness(v.round()),
                      onChangeEnd: (v) =>
                          notifier.setHomeBrightness(v.round()),
                    ),
                  ),
                  SizedBox(
                    width: 48, // deliberate dimension, not spacing
                    child: Text(
                      '${home.brightness}%',
                      textAlign: TextAlign.right,
                      style:
                          Theme.of(context).textTheme.bodySmall?.copyWith(
                                color: Theme.of(context)
                                    .colorScheme
                                    .onSurfaceVariant,
                                fontFamily: 'monospace',
                              ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _ConnectionStatusRow extends StatelessWidget {
  const _ConnectionStatusRow({
    required this.label,
    required this.onForget,
  });
  final String label;
  final VoidCallback onForget;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(
          horizontal: AppSpace.lg, vertical: AppSpace.md),
      child: Row(
        children: [
          const Icon(Icons.wifi, size: 18), // deliberate dimension, not spacing
          const SizedBox(width: AppSpace.sm),
          Expanded(child: Text(label)),
          TextButton.icon(
            icon: const Icon(Icons.wifi_off, size: 16), // deliberate dimension, not spacing
            label: const Text('Forget'),
            onPressed: onForget,
          ),
        ],
      ),
    );
  }
}
