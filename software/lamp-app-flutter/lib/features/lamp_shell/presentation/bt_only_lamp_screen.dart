import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../../nearby/application/nearby_lamps_notifier.dart';

/// Full-screen replacement for the old in-control banner. Shown when the
/// user taps a lamp from "My lamps" whose latest BLE advertisement reports
/// `isMesh: false` — i.e. the lamp's firmware predates the mesh + app
/// protocol. The app CAN'T control it. The lamp just exists in BLE
/// advertising range.
///
/// Two paths the user can take from here:
///
///   1. Configure / control it now — connect this phone's Wi-Fi to the
///      lamp's own access point (its name is the lamp's name), then open
///      `http://192.168.4.1` in a browser. That's the lamp's on-board web
///      UI. The address is shown copy-only (no tap-to-launch) because the
///      user needs to switch their Wi-Fi network first; jumping straight
///      to the URL while still on cellular would just fail.
///   2. Get app + mesh features — flash the latest firmware via
///      `https://update.lamplit.ca`. The URL is tappable — opens the
///      site in the system browser.
class BtOnlyLampScreen extends ConsumerStatefulWidget {
  const BtOnlyLampScreen({super.key, required this.lampId});

  final String lampId;

  @override
  ConsumerState<BtOnlyLampScreen> createState() => _BtOnlyLampScreenState();
}

/// Process-global set of lamp IDs that have already had a BtOnly →
/// Control auto-route fire this app session. Survives remounts of
/// [BtOnlyLampScreen] so the audit-H4 ping-pong (lamp flaps `isMesh`,
/// auto-route to Control, Control bounces back to BtOnly, BtOnly
/// auto-routes back to Control, ad infinitum) can't happen — once a
/// lamp's auto-route fires, every subsequent BtOnly mount for that
/// lamp ignores the fresh-adv signal. The user keeps full manual
/// navigation via the AppBar back button. Reset only on app restart;
/// the auto-route is one-shot convenience, not a steady-state poll.
final Set<String> _btOnlyAlreadyAutoRouted = <String>{};

class _BtOnlyLampScreenState extends ConsumerState<BtOnlyLampScreen> {
  @override
  Widget build(BuildContext context) {
    final lampId = widget.lampId;
    final inventory =
        ref.watch(inventoryNotifierProvider).value ?? const [];
    final lamp = inventory.firstWhereOrNull((l) => l.id == lampId);
    final name = lamp?.name ?? lampId;

    // Auto-recovery: if a fresh adv reports `isMesh: true`, the lamp is
    // mesh-capable after all (the previous adv was stale or the lamp
    // just came online with new firmware). Bounce to ControlScreen so
    // the user isn't trapped on a screen that no longer applies.
    //
    // ONE-SHOT GLOBALLY (audit H4 fix): the "already routed" flag lives
    // in `_btOnlyAutoRoutedProvider` so a remount of this screen
    // (e.g. ControlScreen bounces back to BtOnly on a flaky link) does
    // NOT re-fire the auto-route. The user can navigate manually via
    // the back arrow + tap-from-My-Lamps if they want to re-attempt.
    final isMesh = ref.watch(nearbyLampsNotifierProvider.select(
      (list) => list.firstWhereOrNull((l) => l.id == lampId)?.isMesh,
    ));
    final alreadyRouted = _btOnlyAlreadyAutoRouted.contains(lampId);
    if (!alreadyRouted && isMesh == true) {
      _btOnlyAlreadyAutoRouted.add(lampId);
      final router = GoRouter.maybeOf(context);
      if (router != null) {
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (!mounted) return;
          router.pushReplacement(AppRoutes.control(lampId));
        });
      }
    }

    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          // Explicit back affordance — without this the user is stranded
          // on this screen if they landed via a deep link / initial route
          // (no back stack to auto-derive a leading widget from), and
          // even when pushed from My Lamps the auto-route `pushReplacement`
          // at line ~66 erases the stack on its way to ControlScreen. Pop
          // when possible (returns to My Lamps); otherwise `go` to it
          // explicitly so this is never a dead end.
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
        title: Text(name),
      ),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(
              AppSpace.lg, AppSpace.lg, AppSpace.lg, AppSpace.xl),
          children: [
            Text(
              "This lamp is in Bluetooth range but the app can't talk to "
              "it directly — its firmware doesn't speak the app's mesh "
              "protocol yet.",
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                height: 1.4,
              ),
            ),
            const SizedBox(height: AppSpace.xl),
            const _ActionCard(
              icon: Icons.wifi,
              title: 'Configure it now',
              body:
                  "Join this lamp's own Wi-Fi network from your phone's "
                  "Wi-Fi settings (the SSID is the lamp's name). Once "
                  'connected, open this URL in any browser:',
              url: 'http://192.168.4.1',
              launch: false,
            ),
            const SizedBox(height: AppSpace.lg),
            const _ActionCard(
              icon: Icons.system_update_alt,
              title: 'Get app + mesh features',
              body:
                  'Flash the latest firmware. After the update finishes, '
                  'this lamp will rejoin the mesh and become fully '
                  'controllable from here.',
              url: 'https://update.lamplit.ca',
              launch: true,
            ),
          ],
        ),
      ),
    );
  }
}

/// One of the two action panels. When `launch: true` the URL tile is a
/// tap target that opens the system browser; when false it's a tap-to-
/// copy affordance (because the user needs to switch Wi-Fi networks
/// before the URL would resolve).
class _ActionCard extends StatelessWidget {
  const _ActionCard({
    required this.icon,
    required this.title,
    required this.body,
    required this.url,
    required this.launch,
  });

  final IconData icon;
  final String title;
  final String body;
  final String url;
  final bool launch;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return Container(
      padding: const EdgeInsets.all(AppSpace.lg),
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainer,
        borderRadius: BorderRadius.circular(AppRadius.card),
        border: Border.all(color: colorScheme.outlineVariant),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: colorScheme.primary, size: 22),
              const SizedBox(width: AppSpace.md),
              Expanded(
                child: Text(title, style: textTheme.titleMedium),
              ),
            ],
          ),
          const SizedBox(height: AppSpace.md),
          Text(
            body,
            style: textTheme.bodySmall?.copyWith(
              color: colorScheme.onSurfaceVariant,
              height: 1.4,
            ),
          ),
          const SizedBox(height: AppSpace.md),
          InkWell(
            onTap: () async {
              if (launch) {
                final uri = Uri.parse(url);
                final ok = await launchUrl(uri,
                    mode: LaunchMode.externalApplication);
                if (!ok) {
                  // Fallback: at least put the URL on the clipboard so the
                  // user can paste it into a browser manually.
                  await Clipboard.setData(ClipboardData(text: url));
                  if (context.mounted) {
                    AppSnackbar.info(
                      context, "Couldn't open — copied $url instead",
                    );
                  }
                }
              } else {
                await Clipboard.setData(ClipboardData(text: url));
                if (context.mounted) {
                  AppSnackbar.info(context, 'Copied $url');
                }
              }
            },
            borderRadius: BorderRadius.circular(AppSpace.sm),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: AppSpace.md, vertical: AppSpace.sm),
              decoration: BoxDecoration(
                color: colorScheme.surface,
                borderRadius: BorderRadius.circular(AppSpace.sm),
                border: Border.all(
                  color: colorScheme.primary.withValues(alpha: 0.5),
                ),
              ),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      url,
                      style: TextStyle(
                        color: colorScheme.primary,
                        fontSize: 13,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  Icon(
                    launch ? Icons.open_in_new : Icons.copy,
                    color: colorScheme.onSurfaceVariant,
                    size: 16,
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
