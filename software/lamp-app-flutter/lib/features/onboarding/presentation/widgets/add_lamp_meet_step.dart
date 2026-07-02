import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/friendly_error.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/recolored_critter.dart';
import '../../../inventory/application/inventory_notifier.dart';
import '../../../nearby/application/lamp_route_resolver.dart';
import '../../../nearby/application/nearby_lamps_notifier.dart';
import '../../application/add_lamp_notifier.dart';
import '../../domain/add_lamp_state.dart';

/// The post-claim "meet your lamp" pane. Shown while the lamp reboots and the
/// notifier reconnects in the background; Continue enables once status is
/// `ready`. Gives the user something warm to read during the ~5-12s restart
/// instead of a bare spinner that can error out.
class AddLampMeetStep extends ConsumerWidget {
  const AddLampMeetStep({super.key});

  static LampColor _color(int rgb) =>
      LampColor(r: (rgb >> 16) & 0xFF, g: (rgb >> 8) & 0xFF, b: rgb & 0xFF, w: 0);

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final notifier = ref.read(addLampNotifierProvider.notifier);
    final state = ref.watch(addLampNotifierProvider);
    final textTheme = Theme.of(context).textTheme;
    final name = state.name.isEmpty ? 'your lamp' : state.name;
    final ready = state.status == AddLampStatus.ready;
    final failed = state.status == AddLampStatus.error &&
        state.error == AddLampError.connectFailed;

    return Padding(
      padding: const EdgeInsets.all(AppSpace.xl),
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const Spacer(),
            RecoloredCritter(
              deviceId: state.deviceId,
              shadeColors: [_color(state.shadeRgb)],
              baseColors: [_color(state.baseRgb)],
              size: 120,
            ),
            const SizedBox(height: AppSpace.xl),
            Text(
              'Welcome home, $name.',
              textAlign: TextAlign.center,
              style: textTheme.headlineSmall,
            ),
            const SizedBox(height: AppSpace.md),
            Text(
              "Your stray's waking up. Give it a few seconds to find its feet.",
              textAlign: TextAlign.center,
              style: textTheme.bodyMedium,
            ),
            const SizedBox(height: AppSpace.md),
            Text(
              "From here it's in your care. Help it find its colors, base and "
              'shade, and coax out the little moods and pulses it likes to move '
              "through. Introduce it around, too. It'll make its own friends "
              'among the other lamps, warming from salty to smitten in its own '
              'time.',
              textAlign: TextAlign.center,
              style: textTheme.bodyMedium,
            ),
            const Spacer(),
            if (failed) ...[
              FriendlyError.inline(
                title: "This is taking longer than expected. Make sure the "
                    "lamp has power and your phone's close.",
                rawError: state.errorMessage,
              ),
              const SizedBox(height: AppSpace.md),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton(
                  onPressed: notifier.retryVerify,
                  child: const Text('Try again'),
                ),
              ),
            ] else
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  // Adopt lamps are always mesh — no Wi-Fi setup to show — so
                  // skip the done screen and open the lamp directly.
                  onPressed: ready
                      ? () async {
                          await notifier.finishAdoption();
                          if (!context.mounted) return;
                          notifier.reset();
                          final inv =
                              ref.read(inventoryNotifierProvider).value;
                          final nearby =
                              ref.read(nearbyLampsNotifierProvider);
                          context.go(
                            routeForLamp(state.deviceId, nearby,
                                inventory: inv),
                          );
                        }
                      : null,
                  child: ready
                      ? const Text('Continue')
                      : Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const SizedBox(
                              width: 16,
                              height: 16,
                              child:
                                  CircularProgressIndicator(strokeWidth: 2),
                            ),
                            const SizedBox(width: AppSpace.sm),
                            Text('Rousing $name…'),
                          ],
                        ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
