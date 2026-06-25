import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';

import '../../../../core/theme/brand_colors.dart';
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
    return Padding(
      padding: const EdgeInsets.all(24),
      child: SizedBox(
        width: double.infinity,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            SvgPicture.asset(critter, width: 96, height: 96),
            const SizedBox(height: 12),
            Text(
              'Meet $name',
              textAlign: TextAlign.center,
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 20,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 6),
            const Text(
              "They're yours to shape from here on.",
              textAlign: TextAlign.center,
              style: TextStyle(color: BrandColors.fogGrey, fontSize: 13),
            ),
            const SizedBox(height: 20),
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
                  style: const TextStyle(
                    color: BrandColors.lampWhite,
                    fontSize: 14,
                    height: 1.55,
                  ),
                ),
              ),
            ),
            const SizedBox(height: 16),
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
