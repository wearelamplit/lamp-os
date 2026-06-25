import 'package:flutter/material.dart';

import '../theme/brand_colors.dart';
import 'status_dot.dart';

class LampChip extends StatelessWidget {
  const LampChip({
    super.key,
    required this.name,
    required this.status,
    required this.onTap,
  });

  final String name;
  final StatusKind status;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      borderRadius: BorderRadius.circular(999),
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(999),
          color: BrandColors.auroraBlue.withValues(alpha: 0.18),
          border: Border.all(
            color: BrandColors.auroraBlue.withValues(alpha: 0.4),
          ),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            StatusDot(kind: status, size: 8),
            const SizedBox(width: 8),
            Text(
              name,
              style: const TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: BrandColors.lampWhite,
              ),
            ),
            const SizedBox(width: 6),
            const Icon(Icons.arrow_drop_down, size: 16, color: BrandColors.slateGrey),
          ],
        ),
      ),
    );
  }
}
