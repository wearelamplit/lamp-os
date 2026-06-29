import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../application/add_lamp_notifier.dart';
import '../domain/add_lamp_state.dart';
import 'widgets/add_lamp_done_step.dart';
import 'widgets/add_lamp_name_step.dart';
import 'widgets/add_lamp_password_step.dart';
import 'widgets/add_lamp_scan_step.dart';

class AddLampShell extends ConsumerStatefulWidget {
  const AddLampShell({super.key});

  @override
  ConsumerState<AddLampShell> createState() => _AddLampShellState();
}

class _AddLampShellState extends ConsumerState<AddLampShell> {
  /// Cached at initState so `dispose` can call into the notifier without
  /// touching `ref` after the widget is deactivated (Riverpod 4 blocks
  /// `ref` use in `dispose`).
  late final AddLampNotifier _notifier;

  @override
  void initState() {
    super.initState();
    _notifier = ref.read(addLampNotifierProvider.notifier);
  }

  @override
  void dispose() {
    // Reset the wizard to its initial step on every exit path — back
    // arrow, GoRouter pop, system back. Otherwise `addLampNotifier` is
    // `keepAlive: true` and the user's previous `done` state persists,
    // so the next time they open `/onboarding/add` they land directly
    // on the success screen with no scan list. The Done step's own
    // "Open your lamp" handler also calls reset(); idempotent.
    //
    // Run via `Future.microtask` so the `state = …` write doesn't fire
    // during widget-tree teardown — Riverpod 4 blocks mutations in that
    // window (`_debugCanModifyProviders`). The microtask runs on the
    // very next event-loop turn, after the current frame finalizes.
    //
    // The try/catch is for tests: when the `ProviderContainer` is torn
    // down before the microtask runs, the notifier is already disposed
    // and `state =` throws `UnmountedRefException`. In production the
    // provider is `keepAlive: true`, so the reset always lands safely.
    Future.microtask(() {
      try {
        _notifier.reset();
      } catch (_) {
        // Container disposed before microtask ran — nothing to reset.
      }
    });
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(addLampNotifierProvider);
    final step = state.step;
    final body = switch (step) {
      AddLampStep.scan => const AddLampScanStep(),
      // ponytail: stub replaced by AdoptConfirmStep in Task 4
      AddLampStep.adoptConfirm => const SizedBox.shrink(),
      AddLampStep.name => const AddLampNameStep(),
      AddLampStep.password => const AddLampPasswordStep(),
      AddLampStep.verifying => const AddLampPasswordStep(),
      AddLampStep.done => const AddLampDoneStep(),
    };
    // Hide progress dots on Scan; show from adoptConfirm through Done.
    final showDots = step != AddLampStep.scan;
    return Scaffold(
      appBar: AppBar(
        // Explicit back affordance for the Scan step — pre-fix the user
        // had no way out (GoRouter auto-leading shows up only when
        // canPop is true, which fails on deep-link entry, and the
        // in-flow Back button only appears once the user advances to
        // Name). Audit ux-H2.
        leading: IconButton(
          icon: const Icon(Icons.close),
          onPressed: () {
            final router = GoRouter.maybeOf(context);
            if (router == null) return;
            if (router.canPop()) {
              router.pop();
            } else {
              router.go(AppRoutes.myLamps);
            }
          },
        ),
        title: const Text('Adopt a lamp'),
        bottom: showDots
            ? PreferredSize(
                preferredSize: const Size.fromHeight(8),
                // adoptConfirm=1 maps to dot 0; subtract 1 so
                // adoptConfirm=>0, name=>1, password=>2,
                // verifying=>3, done=>4 against the 5-dot row below.
                child: _ProgressDots(currentIndex: step.index - 1),
              )
            : null,
      ),
      body: body,
    );
  }
}

class _ProgressDots extends StatelessWidget {
  const _ProgressDots({required this.currentIndex});
  final int currentIndex;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Padding(
      padding: const EdgeInsets.only(bottom: AppSpace.sm),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: List.generate(5, (i) {
          final active = i == currentIndex;
          return Container(
            margin: const EdgeInsets.symmetric(horizontal: 4),
            width: active ? 24 : 8,
            height: 8,
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(999),
              color: active
                  ? colorScheme.primary
                  : colorScheme.outline.withValues(alpha: 0.5),
            ),
          );
        }),
      ),
    );
  }
}
