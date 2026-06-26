import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../application/nearby_lamps_notifier.dart';

class NearbyLampsScreen extends ConsumerWidget {
  const NearbyLampsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final lamps = ref.watch(nearbyLampsNotifierProvider);
    return Scaffold(
      appBar: AppBar(
        leading: const BackButtonLeading(),
        title: const Text('Nearby lamps (debug)'),
      ),
      body: lamps.isEmpty
          ? const Center(
              child: Text(
                'Scanning...',
                style: TextStyle(color: BrandColors.fogGrey),
              ),
            )
          : ListView.separated(
              padding: const EdgeInsets.all(16),
              itemCount: lamps.length,
              separatorBuilder: (_, index) => const SizedBox(height: 8),
              itemBuilder: (context, i) {
                final l = lamps[i];
                return Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.white.withValues(alpha: 0.04),
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(
                      color: Colors.white.withValues(alpha: 0.06),
                    ),
                  ),
                  child: Row(
                    children: [
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              l.name.isEmpty ? '(unnamed)' : l.name,
                              style: const TextStyle(
                                color: BrandColors.lampWhite,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            Text(
                              '${l.id} · ${l.rssi} dBm',
                              style: const TextStyle(
                                color: BrandColors.slateGrey,
                                fontSize: 11,
                                fontFamily: 'monospace',
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
                            horizontal: 8,
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
