import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/utils/tap_counter.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/password_prompt_dialog.dart';
import '../../../core/widgets/rename_dialog.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/application/dev_mode.dart';

/// Setup tab: mobile-style row list. Tapping a row drills into a
/// sub-pane (HomeWifi, HomeMode, AdvancedLeds, Knockout). The Home Wi-Fi
/// row has a tap-toggle that flips the SSID between "" and a placeholder
/// without leaving the screen; tapping the chevron-y area drills in to
/// pick the network.
class SetupScreen extends ConsumerWidget {
  const SetupScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Not narrowed via .select: SetupBody consumes 6+ separate state fields
    // (home.*, lamp.name, base.*, shade.*), so a record for .select would
    // touch every changed field anyway. This is the "less hot" of the .select
    // candidates; knockout is the actual exploitable case.
    final async = ref.watch(controlNotifierProvider(lampId));
    return async.when(
      loading: () => const SizedBox.expand(),
      error: (e, _) => FriendlyError.page(
        title: "Couldn't reach your lamp.",
        subtitle:
            "They may have wandered out of range. Bring your phone closer "
            'and try again.',
        rawError: e,
        onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
      ),
      data: (state) => _SetupBody(lampId: lampId, state: state),
    );
  }
}

class _SetupBody extends ConsumerWidget {
  const _SetupBody({required this.lampId, required this.state});
  final String lampId;
  final ControlState state;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // Read the notifier fresh at each tap rather than caching one instance
    // across the build: a mid-session reconnect (password change) can
    // invalidate the control provider, staling out a captured reference.
    ControlNotifier n() => ref.read(controlNotifierProvider(lampId).notifier);
    final hasSsid = state.home.ssid.isNotEmpty;
    final enabled = state.home.enabled;
    final networkBound = state.home.networkBound;
    final String homeSubtitle;
    if (enabled) {
      if (networkBound) {
        homeSubtitle = hasSsid
            ? 'On · ${state.home.ssid} · ${state.home.brightness}%'
            : 'On · not configured';
      } else {
        homeSubtitle = 'On · manual';
      }
    } else {
      homeSubtitle = hasSsid ? 'Off · ${state.home.ssid} saved' : 'Off';
    }
    return ListView(
      padding: const EdgeInsets.only(top: AppSpace.sm, bottom: AppSpace.xxl),
      children: [
        const SettingsGroupHeading('Lamp'),
        SettingsRow(
          icon: Icons.label_outline,
          title: 'Name',
          subtitle: state.lamp.name.isEmpty ? '(unnamed)' : state.lamp.name,
          onTap: () => showRenameDialog(
            context,
            title: 'Rename lamp',
            label: 'Name',
            initial: state.lamp.name,
            onSave: (name) => n().setLampName(name),
          ),
        ),
        SettingsRow(
          icon: Icons.lock_outline,
          title: 'Password',
          subtitle: switch (state.lamp.hasPassword) {
            true => 'Protected',
            false => 'Open · no password',
            null => null,
          },
          onTap: () async {
            final pw = await showPasswordPromptDialog(
              context,
              title: 'Change password',
              subtitle: 'Saving restarts the lamp (~10s).',
              confirmLabel: 'Save',
            );
            if (pw != null) unawaited(n().setLampPassword(pw));
          },
        ),
        const SettingsGroupHeading('Connectivity'),
        SettingsRow(
          icon: Icons.home_outlined,
          title: 'Home Mode',
          subtitle: homeSubtitle,
          onTap: () => context.push(AppRoutes.homeMode(lampId)),
        ),
        SettingsRow(
          icon: Icons.router_outlined,
          title: 'Setup hotspot',
          subtitle: state.lamp.webappEnabled
              ? 'Broadcasts a setup network for 2 min after each power-on'
              : 'Off',
          trailing: Switch(
            value: state.lamp.webappEnabled,
            onChanged: (v) => n().setLampWebappEnabled(v),
          ),
        ),
        const SettingsGroupHeading('LEDs'),
        SettingsRow(
          icon: Icons.lightbulb_outline,
          title: 'LED setup',
          subtitle: 'Base ${state.base.px} · Shade ${state.shade.px} LEDs',
          onTap: () => context.push(AppRoutes.advancedLeds(lampId)),
        ),
        // Debug group appears only in advanced mode.
        if (ref.watch(effectiveAdvancedProvider(lampId))) ...[
          const SettingsGroupHeading('Debug'),
          SettingsRow(
            icon: Icons.bluetooth_searching,
            title: 'Mesh lamps',
            subtitle: 'Fleet roster over the mesh',
            onTap: () => context.push('/mesh-lamps'),
          ),
          // Destructive: wipes NVS and re-adopts. The dialog confirms
          // before firing.
          SettingsRow(
            icon: Icons.restore_outlined,
            title: 'Factory reset',
            subtitle: 'Wipe all settings and re-adopt',
            onTap: () => _showFactoryResetDialog(context, lampId),
          ),
        ],
      ],
    );
  }
}

/// Factory-reset confirmation. Destructive operation, so it goes through a
/// straightforward Cancel/Reset dialog (no text confirmation step like
/// some apps require; the advanced-mode gesture barrier and the
/// confirm-tap are gate enough). On Reset, the notifier sends the
/// settings_blob sentinel and the lamp reboots into factory defaults.
/// The dialog pops before the BLE write returns so the UI doesn't
/// hang on the reboot-disconnect.
///
/// The content area carries a hidden 5-tap zone to the LEFT of the
/// action buttons that enables app-global dev mode (password-gated).
/// Only registers when the user already has session-unlocked advanced
/// mode, keeping the feature undiscoverable without going through the
/// Info-wordmark gesture first.
Future<void> _showFactoryResetDialog(
  BuildContext context,
  String lampId,
) =>
    showDialog<void>(
      context: context,
      builder: (_) => _FactoryResetDialog(
        lampId: lampId,
        outerContext: context,
      ),
    );

class _FactoryResetDialog extends ConsumerStatefulWidget {
  const _FactoryResetDialog({
    required this.lampId,
    required this.outerContext,
  });
  final String lampId;
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
    // Gate on session-advanced (NOT effective). The user must have come
    // through the Info-wordmark 5-tap this session, so a devMode-on app
    // doesn't expose this to anyone who picks up the phone.
    if (!ref.read(advancedSessionProvider(widget.lampId))) return;
    final input = await showPasswordPromptDialog(
      context,
      title: 'Dev mode',
      subtitle: 'Enter the dev-mode password to unlock developer features.',
      confirmLabel: 'Unlock',
    );
    if (input == null) return;
    if (!await devModePasswordMatches(input)) {
      if (!mounted) return;
      AppSnackbar.info(context, 'Wrong password');
      return;
    }
    await ref.read(devModeProvider.notifier).enable();
    if (!mounted) return;
    Navigator.of(context).pop();
    if (widget.outerContext.mounted) {
      AppSnackbar.info(widget.outerContext, 'Dev mode enabled');
    }
  }

  @override
  Widget build(BuildContext dialogCtx) {
    final colorScheme = Theme.of(dialogCtx).colorScheme;
    // Everything lives in `content:` rather than splitting into
    // `actions:`. AlertDialog's actions slot lays out via OverflowBar
    // with unbounded-width intrinsic constraints, which is incompatible
    // with the Expanded GestureDetector used as the hidden 5-tap
    // hotspot. Content area uses normal bounded Flex constraints, so
    // Expanded + Row + buttons layout correctly.
    return AlertDialog(
      title: const Text('Factory reset?'),
      contentPadding: const EdgeInsets.fromLTRB(AppSpace.xl, AppSpace.xl, AppSpace.xl, AppSpace.xl),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(
            "This wipes all settings on this lamp and returns it to its "
            "out-of-box state. You'll need to onboard it again.",
            style: Theme.of(dialogCtx).textTheme.bodyMedium?.copyWith(
              color: colorScheme.onSurfaceVariant,
            ),
          ),
          const SizedBox(height: AppSpace.xl),
          Row(
            children: [
              Expanded(
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: _tap.record,
                  child: const SizedBox(height: 36), // deliberate dimension, not spacing
                ),
              ),
              TextButton(
                onPressed: () => Navigator.of(dialogCtx).pop(),
                child: const Text('Cancel'),
              ),
              const SizedBox(width: AppSpace.sm),
              FilledButton(
                style: FilledButton.styleFrom(
                  backgroundColor: colorScheme.error,
                ),
                onPressed: () async {
                  await ref
                      .read(controlNotifierProvider(widget.lampId).notifier)
                      .factoryReset();
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
