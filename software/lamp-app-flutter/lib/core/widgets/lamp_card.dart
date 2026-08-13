import 'package:flutter/material.dart';
import '../theme/app_spacing.dart';

class LampCard extends StatelessWidget {
  const LampCard({required this.child, this.padding, super.key});

  final Widget child;
  final EdgeInsets? padding;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: padding ?? const EdgeInsets.all(AppSpace.lg),
        child: child,
      ),
    );
  }
}
