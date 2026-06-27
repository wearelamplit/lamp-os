import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/lamp_card.dart';
import '../application/nearby_lamps_notifier.dart';

class NearbyLampsScreen extends ConsumerWidget {
  const NearbyLampsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final lamps = ref.watch(nearbyLampsNotifierProvider);
    final textTheme = Theme.of(context).textTheme;
    return Scaffold(
      appBar: AppBar(
        leading: const BackButtonLeading(),
        title: const Text('Nearby lamps (debug)'),
      ),
      body: lamps.isEmpty
          ? Center(
              child: Text(
                'Scanning...',
                style: textTheme.bodyMedium,
              ),
            )
          : ListView.separated(
              padding: const EdgeInsets.all(AppSpace.lg),
              itemCount: lamps.length,
              separatorBuilder: (_, index) => const SizedBox(height: AppSpace.sm),
              itemBuilder: (context, i) {
                final l = lamps[i];
                return LampCard(
                  padding: const EdgeInsets.all(AppSpace.md),
                  child: Row(
                    children: [
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              l.name.isEmpty ? '(unnamed)' : l.name,
                              style: textTheme.titleMedium,
                            ),
                            Text(
                              '${l.id} · ${l.rssi} dBm',
                              style: textTheme.bodySmall?.copyWith(
                                fontFamily: 'monospace',
                                fontSize: 11,
                              ),
                            ),
                          ],
                        ),
                      ),
                      Semantics(
                        label: l.isFactoryDefault
                            ? 'factory default lamp'
                            : 'configured lamp',
                        excludeSemantics: true,
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: AppSpace.sm,
                            vertical: 4,
                          ),
                          decoration: BoxDecoration(
                            borderRadius: BorderRadius.circular(999),
                            color: (l.isFactoryDefault
                                    ? BrandColors.amberGold
                                    : BrandColors.lumenGreen)
                                .withValues(alpha: 0.18),
                          ),
                          child: Text(
                            l.isFactoryDefault ? 'factory' : 'configured',
                            style: TextStyle(
                              fontSize: 10,
                              color: l.isFactoryDefault
                                  ? BrandColors.amberGold
                                  : BrandColors.lumenGreen,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                );
              },
            ),
    );
  }
}
