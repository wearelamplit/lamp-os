import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/theme/brand_extras.dart';
import '../../../core/widgets/lamp_chip.dart';
import '../../../features/control/application/control_notifier.dart';
import '../../../features/control/presentation/control_screen.dart';
import '../../../core/widgets/status_dot.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../social/presentation/social_screen.dart';
import '../../wisp/application/wisp_notifier.dart';
import 'expressions_screen.dart';
import 'info_screen.dart';

/// Bottom-nav tabs for the lamp shell. Wisp used to be a bottom-nav
/// destination but moved out: when only one wisp is painting a given
/// lamp (enforced by the wisp-side multi-wisp coordination), the floating
/// orb indicator is already onscreen advertising it. Tapping the orbs
/// five times unlocks the dedicated wisp config route (`/lamp/:id/wisp`).
/// Same gesture pattern as the Lamplit-wordmark advanced-unlock.
enum LampTab { control, expressions, social, info }

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

  NavigationDestination _destination(
      IconData icon, String label, bool selected) {
    final iconWidget = Icon(icon, size: 22);
    if (!selected) return NavigationDestination(icon: iconWidget, label: label);
    final gradient = context.brandExtras.chromeGradient;
    final primary = Theme.of(context).colorScheme.primary;
    return NavigationDestination(
      icon: Container(
        padding: const EdgeInsets.symmetric(
            horizontal: AppSpace.lg, vertical: AppSpace.xs),
        decoration: BoxDecoration(
          gradient: gradient,
          borderRadius: BorderRadius.circular(999), // pill shape, not spacing
          boxShadow: [
            BoxShadow(
              color: primary.withValues(alpha: 0.3),
              blurRadius: 8,
              offset: const Offset(0, 2),
            ),
          ],
        ),
        child: iconWidget,
      ),
      label: label,
    );
  }

  @override
  Widget build(BuildContext context) {
    // Keep the control connection alive across tab switches. Without this
    // watch, switching to Expressions or Setup unmounts ControlScreen, drops
    // the only listener on controlNotifierProvider, and the provider
    // auto-disposes (incl. ble.disconnect). LampShell unmounting (back to
    // inventory, swap to another lamp) still cleans up because this watch
    // is released with the shell.
    ref.watch(controlNotifierProvider(widget.lampId));

    // Same lifecycle treatment for the wisp notifier even though the
    // Wisp tab is gone from the bottom nav: the WispIndicator on the
    // Setup tab still consumes it, and the dedicated wisp config route
    // pushed from the 5-tap-orbs gesture should reuse the same
    // notifier instance (manual-palette draft + source mode). Without
    // this lamp-shell-level watch, the indicator would dispose the
    // notifier the moment the user navigated away from the Setup tab.
    ref.watch(wispNotifierProvider(widget.lampId));

    final body = switch (_tab) {
      LampTab.control => ControlScreen(lampId: widget.lampId),
      LampTab.expressions => ExpressionsScreen(lampId: widget.lampId),
      LampTab.social => SocialScreen(lampId: widget.lampId),
      LampTab.info => InfoScreen(lampId: widget.lampId),
    };

    final inventory = ref.watch(inventoryNotifierProvider).value;
    final name = inventory
            ?.firstWhereOrNull((l) => l.id == widget.lampId)
            ?.name ??
        widget.lampId;

    // `select` so the shell only rebuilds when the connection state
    // actually flips — not on every shade/base color tick during a drag.
    final connected = ref.watch(controlNotifierProvider(widget.lampId)
        .select((async) => async.value?.connected ?? false));
    // While the user is on a lamp's screen we don't consume the BLE
    // scanner — the scanner is scoped to MyLamps + onboarding/discovery
    // screens. Two-state status is enough here: connected → mesh, not
    // connected → searching (the screen exists precisely because we want
    // to reach this lamp, so "offline" wouldn't be a useful distinction).
    final status =
        connected ? StatusKind.mesh : StatusKind.searching;

    final cs = Theme.of(context).colorScheme;

    return Scaffold(
      appBar: AppBar(
        // The LampChip in `title` routes to My Lamps for the picker — that's
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
      // hear — both confusing. Greying + ignoring the buttons makes
      // the reconnect-in-flight state visible without taking the user
      // off the page they were on. ConnectionBanner (at the top of the
      // tab body) carries the attempt counter; this is the
      // complementary affordance on the bottom nav.
      bottomNavigationBar: IgnorePointer(
        ignoring: !connected,
        child: Opacity(
          opacity: connected ? 1.0 : 0.4,
          child: NavigationBarTheme(
        data: NavigationBarThemeData(
          // Vue active state: `linear-gradient(135deg, auroraBlue, glowPink)`
          // with a soft shadow. Material 3's NavigationBar only lets us set
          // a flat indicator color, so we render the gradient via a custom
          // indicator BoxDecoration.
          indicatorColor: Colors.transparent,
          indicatorShape: const StadiumBorder(),
          labelTextStyle: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.selected)) {
              return TextStyle(
                color: cs.onSurface,
                fontWeight: FontWeight.w600,
                fontSize: 12,
              );
            }
            return TextStyle(color: cs.onSurfaceVariant, fontSize: 12);
          }),
          iconTheme: WidgetStateProperty.resolveWith((states) {
            return IconThemeData(
              color: states.contains(WidgetState.selected)
                  ? cs.onSurface
                  : cs.onSurfaceVariant,
            );
          }),
        ),
        child: NavigationBar(
          // Bottom nav was carrying 6 destinations; "Setup" and "Info"
          // are now reached via the AppBar Configuration gear (Setup =
          // lamp-config hub; Info = About section at its bottom). That
          // leaves four primary modes — the first tab is relabelled
          // "Setup" since it's where most lamp tuning happens.
          selectedIndex: _tab.index,
          onDestinationSelected: (i) =>
              setState(() => _tab = LampTab.values[i]),
          destinations: [
            _destination(Icons.tune, 'Setup', _tab == LampTab.control),
            _destination(
                Icons.auto_awesome, 'Expressions', _tab == LampTab.expressions),
            _destination(Icons.handshake_outlined, 'Social',
                _tab == LampTab.social),
            _destination(
                Icons.info_outline, 'Info', _tab == LampTab.info),
          ],
        ),
      ),
        ),
      ),
    );
  }
}
