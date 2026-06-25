// lib/features/onboarding/presentation/widgets/add_lamp_done_step.dart
import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../../control/application/control_notifier.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../nearby/application/lamp_route_resolver.dart';
import '../../../nearby/application/nearby_lamps_notifier.dart';
import '../../application/add_lamp_notifier.dart';

class AddLampDoneStep extends ConsumerWidget {
  const AddLampDoneStep({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(addLampNotifierProvider);
    // Pre-warm the control provider while the user reads the success screen,
    // so by the time they tap "Open your lamp" the BLE connect + auth + section
    // reads are already in flight (and usually done).
    ref.watch(controlNotifierProvider(state.deviceId));
    // Watch the full nearby list (not just an isMesh boolean) so the
    // route picker below can distinguish "lamp not in range" from
    // "lamp in range with isMesh=false" — those route differently
    // (control + ConnectingView vs. dedicated BT-only pane).
    // This widget is only shown for a few seconds at the end of
    // onboarding, so rebuilding on every nearby tick is fine.
    final nearby = ref.watch(nearbyLampsNotifierProvider);
    final isMesh = nearby
            .firstWhereOrNull((l) => l.id == state.deviceId)
            ?.isMesh ??
        false;
    final name = state.name.isEmpty ? 'Your lamp' : state.name;
    return SingleChildScrollView(
      padding: const EdgeInsets.all(24),
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const Icon(
              Icons.favorite,
              color: BrandColors.glowPink,
              size: 64,
            ),
            const SizedBox(height: 16),
            Text(
              '$name is home!',
              textAlign: TextAlign.center,
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 22,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              isMesh
                  ? "They're already chatting with your other lamps."
                  : "They're tethered to your phone for now.",
              style: const TextStyle(color: BrandColors.fogGrey),
              textAlign: TextAlign.center,
            ),
            const SizedBox(height: 24),
            // Wi-Fi instructions only for legacy lamps that aren't on
            // the mesh — mesh-capable lamps don't need the user to
            // chase the AP.
            if (!isMesh) const _WifiSetupCard(),
            if (!isMesh) const SizedBox(height: 24),
            FilledButton(
              onPressed: () {
                ref.read(addLampNotifierProvider.notifier).reset();
                // routeForLamp keeps BT-only lamps off the
                // ConnectingView and on their dedicated pane. Inventory
                // passed for offline-lamp fallback.
                final inv = ref.read(inventoryNotifierProvider).value;
                context.go(
                  routeForLamp(state.deviceId, nearby, inventory: inv),
                );
              },
              child: Text('Say hi to $name'),
            ),
            const SizedBox(height: 16),
            _WhatsNextCard(name: name),
          ],
        ),
      ),
    );
  }
}

/// Step-by-step instructions for joining the lamp to Wi-Fi via its onboard
/// AP and web UI. Shown on the done step so every freshly-claimed
/// Bluetooth-only lamp gets the same path to the mesh.
///
/// The recommended firmware-update URL is the marketing redirect
/// `update.lamplit.ca`, which points at the latest stable image.
class _WifiSetupCard extends StatelessWidget {
  const _WifiSetupCard();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: BrandColors.lampWhite.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: BrandColors.lampWhite.withValues(alpha: 0.06),
        ),
      ),
      child: const Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.wifi, color: BrandColors.auroraBlue, size: 20),
              SizedBox(width: 8),
              Text(
                'Want them to make friends?',
                style: TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
          SizedBox(height: 8),
          Text(
            "Put this lamp on Wi-Fi and they'll join the mesh — chatting "
            'with your other lamps and accepting wireless updates.',
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 13),
          ),
          SizedBox(height: 14),
          _NumberedStep(
            n: 1,
            text:
                "On your phone's Wi-Fi settings, join the lamp's access "
                "point (its SSID matches the lamp's name).",
          ),
          _NumberedStep(
            n: 2,
            text: "Open a browser and visit http://192.168.4.1 — that's "
                "the lamp's own setup page.",
          ),
          _NumberedStep(
            n: 3,
            text:
                'Enter your home Wi-Fi credentials. The lamp will slip onto '
                'the mesh once they reconnect.',
          ),
          SizedBox(height: 10),
          Text.rich(
            TextSpan(
              children: [
                TextSpan(
                  text: 'Tip: ',
                  style: TextStyle(
                    color: BrandColors.lumenGreen,
                    fontWeight: FontWeight.w600,
                    fontSize: 12,
                  ),
                ),
                TextSpan(
                  text:
                      "while you're there, freshen them up with the latest firmware at ",
                  style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
                ),
                TextSpan(
                  text: 'update.lamplit.ca',
                  style: TextStyle(
                    color: BrandColors.auroraBlue,
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                TextSpan(
                  text: '.',
                  style: TextStyle(color: BrandColors.fogGrey, fontSize: 12),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _NumberedStep extends StatelessWidget {
  const _NumberedStep({required this.n, required this.text});
  final int n;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 22,
            height: 22,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: BrandColors.auroraBlue.withValues(alpha: 0.18),
            ),
            child: Text(
              '$n',
              style: const TextStyle(
                color: BrandColors.auroraBlue,
                fontSize: 12,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 13,
                height: 1.35,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Three concrete first-moves to nudge a freshly-adopted lamp owner toward
/// their first feature exploration. Lives under the "Visit ${name}" CTA so
/// the user can skip past it by tapping the button.
class _WhatsNextCard extends StatelessWidget {
  const _WhatsNextCard({required this.name});
  final String name;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: BrandColors.lampWhite.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: BrandColors.lampWhite.withValues(alpha: 0.06),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.auto_awesome,
                  color: BrandColors.glowPink, size: 20),
              const SizedBox(width: 8),
              Text(
                'First moves with $name',
                style: const TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          const _NextStep(text: 'Pick their colors in the Colors tab.'),
          const _NextStep(
              text:
                  'Give them an expression so they shimmer on their own.'),
          const _NextStep(
              text: "Adopt a friend so they aren't alone."),
        ],
      ),
    );
  }
}

class _NextStep extends StatelessWidget {
  const _NextStep({required this.text});
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Padding(
            padding: EdgeInsets.only(top: 6, right: 8),
            child: Icon(Icons.circle, color: BrandColors.glowPink, size: 5),
          ),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(
                color: BrandColors.fogGrey,
                fontSize: 13,
                height: 1.4,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
