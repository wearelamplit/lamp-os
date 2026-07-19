import 'dart:async';
import 'dart:math';
import 'dart:ui' show ImageFilter;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:go_router/go_router.dart';

import '../routing/router.dart';
import '../routing/routes.dart';
import '../../features/control/application/control_notifier.dart';
import '../../features/control/presentation/widgets/critter_asset.dart';
import 'reaching_lamp_lines.dart';

final _lampRoute =
    RegExp(r'^/lamp/([^/]+)/(control|expressions|setup|wisp)(?:/|$)');

/// The lamp id of a connection-aware route (control, expressions, setup,
/// wisp, and their sub-routes), or null when [uri] isn't one. Every one of
/// these screens keys its connect/disconnect UI on the same
/// `controlNotifierProvider(id)` the gate reads.
String? activeLampId(String uri) => _lampRoute.firstMatch(uri)?.group(1);

/// App-level gate: shows a whimsical blocking overlay whenever the app is
/// actively reaching the lamp of the current route (initial connect, drop, or
/// save-reboot). Mounted above the router so it covers pushed routes too.
///
/// Reads the current location off the [GoRouter] instance rather than
/// `GoRouterState.of(context)`: this widget sits in `MaterialApp.router`'s
/// `builder`, above the Navigator, where no `ModalRoute` exists yet for
/// `GoRouterState.of` to find.
class ReachingLampGate extends ConsumerStatefulWidget {
  const ReachingLampGate({super.key, required this.child});
  final Widget child;

  @override
  ConsumerState<ReachingLampGate> createState() => _ReachingLampGateState();
}

class _ReachingLampGateState extends ConsumerState<ReachingLampGate> {
  late final GoRouter _router = ref.read(appRouterProvider);

  @override
  void initState() {
    super.initState();
    _router.routeInformationProvider.addListener(_onRouteChanged);
  }

  void _onRouteChanged() => setState(() {});

  @override
  void dispose() {
    _router.routeInformationProvider.removeListener(_onRouteChanged);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final id = activeLampId(_router.routeInformationProvider.value.uri.toString());
    Widget? overlay;
    if (id != null) {
      final state = ref.watch(controlNotifierProvider(id));
      final reaching = state.isLoading ||
          state.maybeWhen(data: (d) => !d.connected, orElse: () => false);
      if (reaching) {
        final name = state.maybeWhen(
            data: (d) => d.lamp.name, orElse: () => 'your lamp');
        overlay = _ReachingOverlay(
          lampId: id,
          name: name,
          onPickAnother: () => _router.go(AppRoutes.myLamps),
        );
      }
    }
    final stack = Stack(children: [
      widget.child,
      Positioned.fill(
        child: IgnorePointer(
          ignoring: overlay == null,
          child: AnimatedSwitcher(
            duration: const Duration(milliseconds: 350),
            child: overlay ?? const SizedBox.shrink(),
          ),
        ),
      ),
    ]);
    // While the overlay is up the underlying route must not pop out from
    // under it (system back / iOS edge-swipe); the only way off the gate is
    // its own "pick another lamp" action.
    return PopScope(canPop: overlay == null, child: stack);
  }
}

class _ReachingOverlay extends StatefulWidget {
  const _ReachingOverlay({
    required this.lampId,
    required this.name,
    required this.onPickAnother,
  });
  final String lampId;
  final String name;
  final VoidCallback onPickAnother;

  @override
  State<_ReachingOverlay> createState() => _ReachingOverlayState();
}

class _ReachingOverlayState extends State<_ReachingOverlay>
    with SingleTickerProviderStateMixin {
  late final AnimationController _bounce;
  late final ShuffleBag<String> _bag;
  late String _line;
  Timer? _rotate;

  @override
  void initState() {
    super.initState();
    _bounce = AnimationController(
        vsync: this, duration: const Duration(milliseconds: 900))
      ..repeat(reverse: true);
    _bag = ShuffleBag<String>(reachingLampLines, Random());
    _line = _bag.next();
    _rotate = Timer.periodic(const Duration(seconds: 4),
        (_) => setState(() => _line = _bag.next()));
  }

  @override
  void dispose() {
    _rotate?.cancel();
    _bounce.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final tt = Theme.of(context).textTheme;
    return Semantics(
      liveRegion: true,
      label: 'Reaching your lamp',
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 8, sigmaY: 8),
        child: ColoredBox(
          color: cs.scrim.withValues(alpha: 0.72),
          child: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                AnimatedBuilder(
                  animation: _bounce,
                  builder: (_, child) => Transform.translate(
                    offset: Offset(
                        0, -8 * Curves.easeInOut.transform(_bounce.value)),
                    child: child,
                  ),
                  child: SvgPicture.asset(critterAssetFor(widget.lampId),
                      width: 160, height: 160),
                ),
                const SizedBox(height: 24),
                AnimatedSwitcher(
                  duration: const Duration(milliseconds: 400),
                  child: Text(
                    reachingLampLine(_line, widget.name),
                    key: ValueKey(_line),
                    textAlign: TextAlign.center,
                    style: tt.titleMedium?.copyWith(color: cs.onSurface),
                  ),
                ),
                const SizedBox(height: 24),
                TextButton(
                  onPressed: widget.onPickAnother,
                  child: const Text('← pick another lamp'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
