import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../routing/routes.dart';

/// Drop-in `AppBar.leading` IconButton that pops the current route when
/// possible, falling back to [AppRoutes.myLamps] when there's no back
/// stack (deep-link entries, or the auto-route in BtOnlyLampScreen
/// having erased history). Centralised so every inner-screen AppBar
/// shares the same affordance instead of either relying
/// on GoRouter's unreliable auto-leading or each screen rolling its
/// own.
///
/// The [icon] defaults to `Icons.arrow_back`; pass `Icons.close` for
/// modal-style screens like the AddLamp shell.
class BackButtonLeading extends StatelessWidget {
  const BackButtonLeading({super.key, this.icon = Icons.arrow_back});

  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return IconButton(
      icon: Icon(icon),
      onPressed: () {
        final router = GoRouter.maybeOf(context);
        if (router == null) return;
        if (router.canPop()) {
          router.pop();
        } else {
          router.go(AppRoutes.myLamps);
        }
      },
    );
  }
}
