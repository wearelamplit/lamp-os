import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/widgets/gradient_nav_bar.dart';
import '../../../core/widgets/lamp_chip.dart';
import '../../../features/control/application/control_notifier.dart';
import '../../../features/control/presentation/control_screen.dart';
import '../../../core/widgets/status_dot.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../social/presentation/social_screen.dart';
import '../../wisp/application/wisp_notifier.dart';
import 'expressions_screen.dart';
import 'info_screen.dart';
import 'setup_screen.dart';

/// Bottom-nav tabs for the lamp shell. When only one wisp is painting a
/// given lamp (enforced by the wisp-side multi-wisp coordination), the
/// floating orb indicator is already onscreen advertising it. Tapping the
/// orbs five times unlocks the dedicated wisp config route
/// (`/lamp/:id/wisp`). Same gesture pattern as the Lamplit-wordmark
/// advanced-unlock.
enum LampTab { control, social, expressions, config, info }

class LampShell extends ConsumerStatefulWidget {
  const LampShell({
    super.key,
    required this.lampId,
    this.initialTab = LampTab.control,
  });

  final String lampId;
  final LampTab initialTab;

  @override
  ConsumerState<LampShell> createState() => _LampShellState();
}

class _LampShellState extends ConsumerState<LampShell> {
  late LampTab _tab = widget.initialTab;

  @override
  Widget build(BuildContext context) {
    // Keep the control connection alive across tab switches. Without this
    // watch, switching to another tab unmounts ControlScreen, drops
    // the only listener on controlNotifierProvider, and the provider
    // auto-disposes (incl. ble.disconnect). LampShell unmounting (back to
    // inventory, swap to another lamp) still cleans up because this watch
    // is released with the shell.
    ref.watch(controlNotifierProvider(widget.lampId));

    // Same lifecycle treatment for the wisp notifier even though the
    // Wisp tab is gone from the bottom nav: the WispIndicator on the
    // Colors tab still consumes it, and the dedicated wisp config route
    // pushed from the 5-tap-orbs gesture should reuse the same
    // notifier instance (manual-palette draft + source mode). Without
    // this lamp-shell-level watch, the indicator would dispose the
    // notifier the moment the user navigated away from the Colors tab.
    ref.watch(wispNotifierProvider(widget.lampId));

    final body = switch (_tab) {
      LampTab.control => ControlScreen(lampId: widget.lampId),
      LampTab.expressions => ExpressionsScreen(lampId: widget.lampId),
      LampTab.social => SocialScreen(lampId: widget.lampId),
      LampTab.config => SetupScreen(lampId: widget.lampId),
      LampTab.info => InfoScreen(lampId: widget.lampId),
    };

    final inventory = ref.watch(inventoryNotifierProvider).value;
    final name = inventory
            ?.firstWhereOrNull((l) => l.id == widget.lampId)
            ?.name ??
        widget.lampId;

    // `select` so the shell only rebuilds when the connection state
    // actually flips, not on every shade/base color tick during a drag.
    final connected = ref.watch(controlNotifierProvider(widget.lampId)
        .select((async) => async.value?.connected ?? false));
    // While the user is on a lamp's screen the BLE scanner is untouched;
    // it's scoped to MyLamps + onboarding/discovery screens. Two-state
    // status is enough here: connected → mesh, not connected → searching
    // (the screen exists precisely to reach this lamp, so "offline"
    // wouldn't be a useful distinction).
    final status =
        connected ? StatusKind.mesh : StatusKind.searching;

    return Scaffold(
      appBar: AppBar(
        // The LampChip in `title` routes to My Lamps for the picker; that's
        // the on-screen nav. Skip GoRouter's auto-injected back arrow (now
        // present because LampShell is pushed on top of My Lamps) so the
        // AppBar doesn't carry two redundant nav controls. Android system
        // back-gesture still pops to My Lamps.
        automaticallyImplyLeading: false,
        title: LampChip(
          name: name,
          status: status,
          onTap: () => GoRouter.maybeOf(context)?.push(AppRoutes.myLamps),
        ),
        actions: const [],
      ),
      body: body,
      // Tab nav is gated on the BLE connection. When the link is
      // down (post-disconnect, mid-reconnect), the per-tab views would
      // either render stale data or hang on a write the lamp can't
      // hear, both confusing. Greying + ignoring the buttons makes
      // the reconnect-in-flight state visible without taking the user
      // off the page they were on; ReachingLampGate carries the
      // reconnect messaging above this.
      bottomNavigationBar: IgnorePointer(
        ignoring: !connected,
        child: Opacity(
          opacity: connected ? 1.0 : 0.4,
          child: GradientNavBar(
            selectedIndex: _tab.index,
            onDestinationSelected: (i) =>
                setState(() => _tab = LampTab.values[i]),
            destinations: const [
              (icon: Icons.palette_outlined, label: 'Colors'),
              (icon: Icons.handshake_outlined, label: 'Social'),
              (icon: Icons.auto_awesome, label: 'Expressions'),
              (icon: Icons.settings_outlined, label: 'Config'),
              (icon: Icons.info_outline, label: 'Info'),
            ],
          ),
        ),
      ),
    );
  }
}
