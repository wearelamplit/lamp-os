import 'package:flutter/material.dart';
import 'package:lamp_app/core/theme/app_spacing.dart';

class SectionHeader extends StatelessWidget {
  const SectionHeader(this.label, {super.key});

  final String label;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
      child: Text(label.toUpperCase(), style: Theme.of(context).textTheme.labelLarge),
    );
  }
}
