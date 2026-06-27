import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';

import '../../../../core/theme/app_spacing.dart';
import '../../../control/presentation/widgets/critter_asset.dart';
import '../../application/add_lamp_notifier.dart';

/// Sits between Name and Password. Once the user has given the lamp a name
/// (emotional buy-in is highest here), reassure them in plain English that
/// they'll be able to make the lamp their own — personality, colors,
/// expressions — so they're not bracing for a config marathon on the next
/// screen.
class AddLampMeetStep extends ConsumerWidget {
  const AddLampMeetStep({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(addLampNotifierProvider);
    final notifier = ref.read(addLampNotifierProvider.notifier);
    // critterIndex isn't picked until submit() persists the lamp, so fall
    // back to the deviceId-hash variant — the resolver in critter_asset
    // does that for us when index is null.
    final critter = critterAssetFor(
      critterIndex: null,
      deviceId: state.deviceId,
    );
    final name = state.name.isEmpty ? 'your lamp' : state.name;
    final textTheme = Theme.of(context).textTheme;
    return Padding(
      padding: const EdgeInsets.all(AppSpace.xl),
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            SvgPicture.asset(critter, width: 96, height: 96),
            const SizedBox(height: AppSpace.md),
            Text(
              'Meet $name',
              textAlign: TextAlign.center,
              style: textTheme.headlineSmall,
            ),
            const SizedBox(height: 6),
            Text(
              "They're yours to shape from here on.",
              textAlign: TextAlign.center,
              style: textTheme.bodyMedium,
            ),
            const SizedBox(height: AppSpace.xl),
            Expanded(
              child: SingleChildScrollView(
                child: Text(
                  "You can pick the colors $name wears, layer little "
                  'expressions so they shimmer on their own, and even tune '
                  'their personality — how they greet the other lamps they '
                  "meet and how calm they get around home. Nothing to set up "
                  "now; it's all a tap away in $name's tabs, whenever you're "
                  'ready.',
                  textAlign: TextAlign.center,
                  style: textTheme.bodyLarge?.copyWith(height: 1.55),
                ),
              ),
            ),
            const SizedBox(height: AppSpace.lg),
            Row(
              children: [
                TextButton(
                  onPressed: notifier.previous,
                  child: const Text('Back'),
                ),
                const Spacer(),
                FilledButton(
                  onPressed: notifier.next,
                  child: const Text('Sounds good'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
