import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../../../core/theme/brand_colors.dart';

class OnboardingPlaceholder extends StatelessWidget {
  const OnboardingPlaceholder({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              'No lamps in your care yet',
              style: TextStyle(
                color: BrandColors.lampWhite,
                fontWeight: FontWeight.w600,
                fontSize: 18,
              ),
            ),
            const SizedBox(height: 12),
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 32),
              child: Text(
                'Find a stray nearby and bring them home.',
                textAlign: TextAlign.center,
                style: TextStyle(color: BrandColors.fogGrey),
              ),
            ),
            const SizedBox(height: 24),
            FilledButton.icon(
              // `push` so the add-lamp wizard's back/cancel returns here.
              onPressed: () => context.push('/onboarding/add'),
              icon: const Icon(Icons.favorite),
              label: const Text('Adopt a lamp'),
            ),
            const SizedBox(height: 12),
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
