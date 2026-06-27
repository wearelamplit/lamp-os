import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../../../core/theme/app_spacing.dart';

class OnboardingPlaceholder extends StatelessWidget {
  const OnboardingPlaceholder({super.key});

  @override
  Widget build(BuildContext context) {
    final textTheme = Theme.of(context).textTheme;
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              'No lamps in your care yet',
              style: textTheme.titleLarge,
            ),
            const SizedBox(height: AppSpace.md),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: AppSpace.xxl),
              child: Text(
                'Find a stray nearby and bring them home.',
                textAlign: TextAlign.center,
                style: textTheme.bodyMedium,
              ),
            ),
            const SizedBox(height: AppSpace.xl),
            FilledButton.icon(
              // `push` so the add-lamp wizard's back/cancel returns here.
              onPressed: () => context.push('/onboarding/add'),
              icon: const Icon(Icons.favorite),
              label: const Text('Adopt a lamp'),
            ),
            const SizedBox(height: AppSpace.md),
            TextButton(
              // `push` so the debug scan screen's back arrow returns here.
              onPressed: () => context.push('/devices'),
              child: const Text('Live scan (debug)'),
            ),
          ],
        ),
      ),
    );
  }
}
