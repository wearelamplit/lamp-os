import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../features/control/presentation/knockout_screen.dart';
import '../../features/firmware/application/cached_firmware_notifier.dart';
import '../../features/firmware/presentation/firmware_cache_screen.dart';
import '../../features/inventory/application/inventory_notifier.dart';
import '../../features/inventory/presentation/my_lamps_screen.dart';
import '../../features/lamp_shell/presentation/add_expression_picker_screen.dart';
import '../../features/lamp_shell/presentation/advanced_leds_screen.dart';
import '../../features/lamp_shell/presentation/bt_only_lamp_screen.dart';
import '../../features/lamp_shell/presentation/expression_editor_screen.dart';
import '../../features/lamp_shell/presentation/home_mode_screen.dart';
import '../../features/lamp_shell/presentation/lamp_shell.dart';
import '../../features/lamp_shell/presentation/setup_screen.dart';
import '../widgets/back_button_leading.dart';
import '../../features/nearby/presentation/nearby_lamps_screen.dart';
import '../../features/onboarding/presentation/add_lamp_shell.dart';
import '../../features/onboarding/presentation/onboarding_placeholder.dart';
import '../../features/wisp/presentation/wisp_config_screen.dart';
import 'routes.dart';

part 'router.g.dart';

@Riverpod(keepAlive: true, name: 'appRouterProvider')
GoRouter appRouter(Ref ref) {
  // Re-run the router's redirect when the inventory provider changes —
  // GoRouter doesn't watch Riverpod itself, so without this the redirect
  // fires once on initial navigation and never again. Without the
  // listener, the very first frame (inventory still loading → redirect
  // returns null) freezes the route until the user navigates manually.
  final refresh = ValueNotifier<int>(0);
  ref.listen(inventoryNotifierProvider, (_, next) {
    refresh.value++;
    // Background-fetch the per-variant firmware for each lamp the user
    // owns so a venue without internet can still push updates. The cache
    // notifier coalesces concurrent triggers and ignores network errors
    // — failure leaves the previous cached copy in place.
    final list = next?.value;
    if (list != null && list.isNotEmpty) {
      ref.read(cachedFirmwareNotifierProvider.notifier).syncForInventory(list);
    }
  });
  ref.onDispose(refresh.dispose);

  return GoRouter(
    // Start on /onboarding so the empty-inventory case lands directly on
    // OnboardingPlaceholder without briefly mounting MyLampsScreen (which
    // would kick the BLE scanner — an unwelcome side effect during the
    // loading window AND in widget tests that don't override fbp).
    // Non-empty inventories bounce to /lamps via the redirect below
    // once the inventory provider's first emission arrives.
    initialLocation: AppRoutes.onboarding,
    refreshListenable: refresh,
    redirect: (context, state) {
      final inv = ref.read(inventoryNotifierProvider).value;
      if (inv == null) return null; // still loading
      final loc = state.uri.toString();
      // Empty inventory → land on onboarding, but allow the user to
      // navigate forward into the AddLamp flow and the debug devices view.
      if (inv.isEmpty) {
        if (loc.startsWith('/onboarding') || loc.startsWith('/devices')) {
          return null;
        }
        return AppRoutes.onboarding;
      }
      // Non-empty inventory: an explicit landing on /onboarding (e.g.
      // legacy deep link or the empty-state placeholder) should bounce
      // to /lamps. Everywhere else, leave navigation alone.
      if (loc == AppRoutes.onboarding) {
        return AppRoutes.myLamps;
      }
      return null;
    },
    routes: [
      GoRoute(
        path: AppRoutes.onboarding,
        builder: (_, _) => const OnboardingPlaceholder(),
      ),
      GoRoute(
        path: AppRoutes.myLamps,
        builder: (_, _) => const MyLampsScreen(),
      ),
      GoRoute(
        path: AppRoutes.firmwareCache,
        builder: (_, _) => const FirmwareCacheScreen(),
      ),
      GoRoute(
        path: '/lamp/:id/control',
        builder: (_, state) =>
            LampShell(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/lamp/:id/bt-only',
        builder: (_, state) =>
            BtOnlyLampScreen(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/lamp/:id/expressions',
        builder: (_, state) => LampShell(
          lampId: state.pathParameters['id']!,
          initialTab: LampTab.expressions,
        ),
      ),
      // Configuration drilldown — pushed from the gear icon in the
      // LampShell AppBar (so it stacks on top of the shell instead of
      // mode-replacing). Bundles the old standalone "Setup" tab body +
      // the old "Info" tab content as an About section at the bottom.
      // Bottom nav no longer carries Setup/Info; this route is the
      // single point of entry.
      GoRoute(
        path: '/lamp/:id/setup',
        builder: (_, state) => Scaffold(
          appBar: AppBar(
            leading: const BackButtonLeading(),
            title: const Text('Configuration'),
          ),
          body: SetupScreen(lampId: state.pathParameters['id']!),
        ),
      ),
      // /info kept as a redirect to /setup — there's no separate Info
      // screen any more; About content lives at the bottom of /setup.
      // Pre-existing routes / deep-links still resolve.
      GoRoute(
        path: '/lamp/:id/info',
        redirect: (_, state) =>
            '/lamp/${state.pathParameters['id']}/setup',
      ),
      // /setup/wifi was the old Home Wi-Fi pane; its UI was merged into
      // the Home Mode pane below. Keep the route pointed at HomeModeScreen
      // so any external bookmarks / deep links continue to resolve.
      GoRoute(
        path: '/lamp/:id/setup/wifi',
        builder: (_, state) =>
            HomeModeScreen(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/lamp/:id/setup/home-mode',
        builder: (_, state) =>
            HomeModeScreen(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/lamp/:id/setup/advanced-leds',
        builder: (_, state) =>
            AdvancedLedsScreen(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/onboarding/add',
        builder: (_, _) => const AddLampShell(),
      ),
      GoRoute(
        path: '/devices',
        builder: (_, state) => const NearbyLampsScreen(),
      ),
      GoRoute(
        path: '/lamp/:id/setup/knockout',
        builder: (_, state) =>
            KnockoutScreen(lampId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/lamp/:id/expressions/new',
        builder: (_, state) => AddExpressionPickerScreen(
          lampId: state.pathParameters['id']!,
        ),
      ),
      GoRoute(
        path: '/lamp/:id/expressions/:type/:target',
        builder: (_, state) => ExpressionEditorScreen(
          lampId: state.pathParameters['id']!,
          typeKey: state.pathParameters['type']!,
          targetKey: int.parse(state.pathParameters['target']!),
        ),
      ),
      GoRoute(
        path: '/lamp/:id/wisp',
        builder: (_, state) =>
            WispConfigScreen(lampId: state.pathParameters['id']!),
      ),
    ],
  );
}
