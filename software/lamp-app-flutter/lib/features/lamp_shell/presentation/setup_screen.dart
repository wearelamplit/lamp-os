import 'dart:convert';

import 'package:cryptography/cryptography.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/utils/tap_counter.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/password_prompt_dialog.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';
import '../../inventory/application/inventory_notifier.dart';

/// SHA-256 of the dev-mode unlock password. The password itself isn't
/// stored anywhere in the app binary — only this hash — so a reverse-
/// engineer pulling strings from the APK doesn't get the secret. They'd
/// need a rainbow-table or dictionary match against this hash, which the
/// uncommon word at the source makes slow but not impossible. "Relatively
/// secure" — gates casual discovery, not a determined adversary.
const _kDevModePasswordHashHex =
    'f7bc3d20eccd3fe6522c1c59e8752e7d85a33d66a560506dc0c83858bf4c2156';

Future<bool> _devModePasswordMatches(String input) async {
  final hash = await Sha256().hash(utf8.encode(input));
  final hex = hash.bytes
      .map((b) => b.toRadixString(16).padLeft(2, '0'))
      .join();
  return hex == _kDevModePasswordHashHex;
}

/// Setup tab — mobile-style row list. Tapping a row drills into a
/// sub-pane (HomeWifi, HomeMode, AdvancedLeds, Knockout). The Home Wi-Fi
/// row has a tap-toggle that flips the SSID between "" and a placeholder
/// without leaving the screen; tapping the chevron-y area drills in to
/// pick the network / enter the password.
class SetupScreen extends ConsumerWidget {
  const SetupScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Not narrowed via .select: SetupBody consumes 6+ separate state
    // fields (home.*, lamp.name, base.*, shade.*). Building a record
    // for .select would touch every changed field anyway. Audit
    // perf-H3 noted this is the "less hot" of the three .select
    // candidates — knockout is the actual exploitable case (W5.2).
    final async = ref.watch(controlNotifierProvider(lampId));
    return async.when(
      loading: () => ConnectingView(deviceId: lampId),
      error: (e, _) => FriendlyError.page(
        title: "Couldn't reach your lamp.",
        subtitle:
            "They may have wandered out of range. Bring your phone closer "
            'and try again.',
        rawError: e,
        onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
      ),
      data: (state) => DisconnectAwareBody(
        lampId: lampId,
        child: _SetupBody(lampId: lampId, state: state),
      ),
    );
  }
}

class _SetupBody extends ConsumerWidget {
  const _SetupBody({required this.lampId, required this.state});
  final String lampId;
  final ControlState state;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = ref.read(controlNotifierProvider(lampId).notifier);
    final hasSsid = state.home.ssid.isNotEmpty;
    final enabled = state.home.enabled;
    final String homeSubtitle;
    if (enabled) {
      homeSubtitle = hasSsid
          ? '${state.home.ssid} · ${state.home.brightness}%'
          : 'On · not configured';
    } else {
      homeSubtitle = hasSsid ? 'Off · ${state.home.ssid} saved' : 'Off';
    }
    return ListView(
      padding: const EdgeInsets.only(top: 8, bottom: 32),
      children: [
        const SettingsGroupHeading('Lamp'),
        SettingsRow(
          icon: Icons.label_outline,
          title: 'Name',
          subtitle: state.lamp.name.isEmpty ? '(unnamed)' : state.lamp.name,
          onTap: () => _showRenameDialog(context, state.lamp.name, n.setLampName),
        ),
        SettingsRow(
          icon: Icons.lock_outline,
          title: 'Password',
          subtitle: 'Tap to change',
          onTap: () => _showPasswordDialog(context, lampId, n.setLampPassword),
        ),
        const SettingsGroupHeading('Connectivity'),
        SettingsRow(
          icon: Icons.home_outlined,
          title: 'Home Mode',
          subtitle: homeSubtitle,
          trailing: Switch(
            value: enabled,
            // Soft toggle: flips the enabled flag without wiping the saved
            // SSID/password. Destructive "Forget network" lives inside the
            // Home Mode pane. First-time on (no SSID yet): drill into the
            // pane so the user can pick a network.
            onChanged: (v) {
              n.setHomeEnabled(v);
              if (v && !hasSsid) {
                context.push(AppRoutes.homeMode(lampId));
              }
            },
          ),
          onTap: () => context.push(AppRoutes.homeMode(lampId)),
        ),
        const SettingsGroupHeading('LEDs'),
        // Per-pixel knockout now nests inside LED setup (under the base
        // strip) — it only masks base pixels.
        // Always-on entry point: LED count is always editable, byte-order
        // pickers (wiring-corruption footgun) are gated inside the
        // destination screen on the raw session-advanced flag.
        SettingsRow(
          icon: Icons.memory,
          title: 'LED setup',
          subtitle:
              'Base ${state.base.px}×${state.base.byteOrder} · '
              'Shade ${state.shade.px}×${state.shade.byteOrder}',
          onTap: () => context.push(AppRoutes.advancedLeds(lampId)),
        ),
        if (ref.watch(effectiveAdvancedProvider(lampId)))
          SettingsRow(
            icon: Icons.wifi_tethering,
            title: 'Boot-time setup AP',
            subtitle: state.lamp.webappEnabled
                ? 'On · 2 min after every power-cycle'
                : 'Off',
            trailing: Switch(
              value: state.lamp.webappEnabled,
              onChanged: (v) => n.setLampWebappEnabled(v),
            ),
          ),
        // Nearby lamps (debug) — same advanced gate. Pre-fix this
        // /devices route was only reachable from the empty-state
        // OnboardingPlaceholder, becoming unreachable in steady state
        // once a lamp was adopted. Audit ux-H1.
        if (ref.watch(effectiveAdvancedProvider(lampId)))
          SettingsRow(
            icon: Icons.bluetooth_searching,
            title: 'Nearby lamps (debug)',
            subtitle: 'Live BLE adv stream',
            onTap: () => context.push('/devices'),
          ),
        // Dev-mode-only — the per-variant firmware cache inspector is
        // useful for fleet operators debugging OTA pipelines, not for
        // an advanced-but-not-dev user.
        if (state.lamp.devMode)
          SettingsRow(
            icon: Icons.folder_zip_outlined,
            title: 'Cached firmwares',
            subtitle: 'Per-variant binaries on this phone',
            onTap: () => context.push(AppRoutes.firmwareCache),
          ),
        // Factory reset — also gated on session-advanced. Destructive:
        // wipes NVS and re-adopts. Dialog confirms before firing.
        if (ref.watch(effectiveAdvancedProvider(lampId)))
          SettingsRow(
            icon: Icons.restore_outlined,
            title: 'Factory reset',
            subtitle: 'Wipe all settings and re-adopt',
            onTap: () => _showFactoryResetDialog(context, lampId, n),
          ),
      ],
    );
  }
}

/// Lightweight rename modal — keeps the row tappable without taking the
/// user to a full sub-screen for a one-field edit. Hosted by a
/// `StatefulWidget` so the `TextEditingController` has a lifecycle to
/// dispose on (a plain `showDialog` closure leaks the controller).
Future<void> _showRenameDialog(
  BuildContext context,
  String initial,
  ValueChanged<String> onSave,
) =>
    showDialog<void>(
      context: context,
      builder: (_) => _RenameDialog(initial: initial, onSave: onSave),
    );

class _RenameDialog extends StatefulWidget {
  const _RenameDialog({required this.initial, required this.onSave});
  final String initial;
  final ValueChanged<String> onSave;

  @override
  State<_RenameDialog> createState() => _RenameDialogState();
}

class _RenameDialogState extends State<_RenameDialog> {
  late final TextEditingController _ctrl =
      TextEditingController(text: widget.initial);

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Rename lamp',
          style: TextStyle(color: BrandColors.lampWhite)),
      content: TextField(
        controller: _ctrl,
        autofocus: true,
        decoration: const InputDecoration(labelText: 'Name'),
        style: const TextStyle(color: BrandColors.lampWhite),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () {
            widget.onSave(_ctrl.text.trim());
            Navigator.of(context).pop();
          },
          child: const Text('Save'),
        ),
      ],
    );
  }
}

/// Password change modal — same shape as the rename dialog, but the field
/// is obscured by default with an eye-toggle reveal. No old-password
/// confirmation: the user is already authenticated for this BLE session,
/// that's the gate. Empty passwords are rejected by the firmware path the
/// same way they would be at onboarding, so we guard for non-empty here.
Future<void> _showPasswordDialog(
  BuildContext context,
  String lampId,
  Future<void> Function(String) onSave,
) =>
    showDialog<void>(
      context: context,
      builder: (_) => _PasswordDialog(onSave: onSave),
    );

class _PasswordDialog extends StatefulWidget {
  const _PasswordDialog({required this.onSave});
  final Future<void> Function(String) onSave;

  @override
  State<_PasswordDialog> createState() => _PasswordDialogState();
}

class _PasswordDialogState extends State<_PasswordDialog> {
  final _ctrl = TextEditingController();
  bool _obscured = true;

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Change password',
          style: TextStyle(color: BrandColors.lampWhite)),
      content: TextField(
        controller: _ctrl,
        autofocus: true,
        obscureText: _obscured,
        decoration: InputDecoration(
          labelText: 'New password',
          suffixIcon: IconButton(
            icon: Icon(_obscured ? Icons.visibility : Icons.visibility_off),
            onPressed: () => setState(() => _obscured = !_obscured),
          ),
        ),
        style: const TextStyle(color: BrandColors.lampWhite),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: () {
            final pw = _ctrl.text;
            if (pw.isEmpty) return;
            // Fire-and-forget — setLampPassword drives lampSaveStatus, so
            // the user sees the "Saving changes…" overlay while the lamp
            // reboots and reconnects.
            widget.onSave(pw);
            Navigator.of(context).pop();
          },
          child: const Text('Save'),
        ),
      ],
    );
  }
}

/// Factory-reset confirmation. Destructive operation, so we go through a
/// straightforward Cancel/Reset dialog (no text confirmation step like
/// some apps require — the advanced-mode gesture barrier and the
/// confirm-tap are gate enough). On Reset, the notifier sends the
/// settings_blob sentinel and the lamp reboots into factory defaults.
/// We pop the dialog before the BLE write returns so the UI doesn't
/// hang on the reboot-disconnect.
///
/// The content area carries a hidden 5-tap zone to the LEFT of the
/// action buttons that toggles the lamp's persisted `devMode` flag.
/// Only registers when the user already has session-unlocked advanced
/// mode — keeps the feature undiscoverable without going through the
/// Info-wordmark gesture first.
Future<void> _showFactoryResetDialog(
  BuildContext context,
  String lampId,
  ControlNotifier notifier,
) =>
    showDialog<void>(
      context: context,
      builder: (_) => _FactoryResetDialog(
        lampId: lampId,
        notifier: notifier,
        outerContext: context,
      ),
    );

class _FactoryResetDialog extends ConsumerStatefulWidget {
  const _FactoryResetDialog({
    required this.lampId,
    required this.notifier,
    required this.outerContext,
  });
  final String lampId;
  final ControlNotifier notifier;
  // GoRouter on post-reset navigation needs the parent route tree, not
  // the dialog's overlay route. Captured at show-time so the post-pop
  // jump to my-lamps lands on the right router.
  final BuildContext outerContext;

  @override
  ConsumerState<_FactoryResetDialog> createState() =>
      _FactoryResetDialogState();
}

class _FactoryResetDialogState extends ConsumerState<_FactoryResetDialog> {
  late final TapCounter _tap;

  @override
  void initState() {
    super.initState();
    _tap = TapCounter(
      count: 5,
      window: const Duration(seconds: 2),
      onTriggered: _onDevModeTap,
    );
  }

  Future<void> _onDevModeTap() async {
    // Gate the whole gesture on session-advanced (NOT effective). The
    // user must have come through the Info-wordmark 5-tap this session;
    // we don't want a devMode-on lamp to expose the toggle to anyone
    // who picks up the phone.
    if (!ref.read(advancedSessionProvider(widget.lampId))) return;
    final async = ref.read(controlNotifierProvider(widget.lampId));
    final cur = async.value?.lamp.devMode ?? false;
    final next = !cur;
    // Enabling requires the password — relatively secure gate on top of
    // the 5-tap-hotspot + advanced-session prerequisites. Disable is
    // free so a stuck-on lamp can always be returned to safe state.
    if (next) {
      final input = await showPasswordPromptDialog(
        context,
        title: 'Dev mode',
        subtitle: 'Enter the dev-mode password to unlock this lamp.',
        confirmLabel: 'Unlock',
      );
      if (input == null) return;
      if (!await _devModePasswordMatches(input)) {
        if (!mounted) return;
        AppSnackbar.info(context, 'Wrong password');
        return;
      }
    }
    try {
      await widget.notifier
          .writeSettingsBlob({'lamp': {'devMode': next}}, reboot: false);
    } catch (_) {
      if (!mounted) return;
      AppSnackbar.info(context, 'Dev mode toggle failed');
      return;
    }
    // Mirror to inventory immediately so `effectiveAdvancedProvider`
    // (which reads from inventory, not control state) reflects the new
    // value without waiting for a reconnect-driven section reload.
    await ref
        .read(inventoryNotifierProvider.notifier)
        .updateDevMode(widget.lampId, next);
    if (!mounted) return;
    // On enable, close the factory reset modal — the user is done with
    // it; leaving it open after a successful unlock looks like the tap
    // did nothing. The snackbar shows on the parent route so it stays
    // visible after the pop.
    if (next) {
      Navigator.of(context).pop();
    }
    if (widget.outerContext.mounted) {
      AppSnackbar.info(
        widget.outerContext,
        next ? 'Dev mode enabled' : 'Dev mode disabled',
      );
    }
  }

  @override
  Widget build(BuildContext dialogCtx) {
    // Everything lives in `content:` rather than splitting into
    // `actions:`. AlertDialog's actions slot lays out via OverflowBar
    // with unbounded-width intrinsic constraints, which is incompatible
    // with the Expanded GestureDetector we use as the hidden 5-tap
    // hotspot. Content area uses normal bounded Flex constraints, so
    // Expanded + Row + buttons layout correctly.
    return AlertDialog(
      backgroundColor: BrandColors.midnightBlack,
      title: const Text('Factory reset?',
          style: TextStyle(color: BrandColors.lampWhite)),
      contentPadding: const EdgeInsets.fromLTRB(24, 20, 24, 20),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Text(
            "This wipes all settings on this lamp and returns it to its "
            "out-of-box state. You'll need to onboard it again.",
            style: TextStyle(color: BrandColors.fogGrey),
          ),
          const SizedBox(height: 20),
          Row(
            children: [
              Expanded(
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: _tap.record,
                  child: const SizedBox(height: 36),
                ),
              ),
              TextButton(
                onPressed: () => Navigator.of(dialogCtx).pop(),
                child: const Text('Cancel'),
              ),
              const SizedBox(width: 8),
              FilledButton(
                style: FilledButton.styleFrom(
                  backgroundColor: BrandColors.error,
                ),
                onPressed: () async {
                  await widget.notifier.factoryReset();
                  if (!dialogCtx.mounted) return;
                  Navigator.of(dialogCtx).pop();
                  if (!widget.outerContext.mounted) return;
                  GoRouter.of(widget.outerContext).go(AppRoutes.myLamps);
                },
                child: const Text('Reset'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
