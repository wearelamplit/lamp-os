import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../control/application/control_notifier.dart';
import '../../control/domain/sections.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/connection_banner.dart';
import '../domain/expression_meta.dart';

/// Entry point for adding a new expression. Replaces the previous "open the
/// editor with a blank draft" flow: now the user picks Target first
/// (Shade / Base / Both), then picks one of the expression types from a
/// list of friendly cards. Combos already in use are disabled in-place.
class AddExpressionPickerScreen extends ConsumerStatefulWidget {
  const AddExpressionPickerScreen({super.key, required this.lampId});

  final String lampId;

  @override
  ConsumerState<AddExpressionPickerScreen> createState() =>
      _AddExpressionPickerScreenState();
}

class _AddExpressionPickerScreenState
    extends ConsumerState<AddExpressionPickerScreen> {
  int _target = 3; // TARGET_BOTH default

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(controlNotifierProvider(widget.lampId));
    return Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () {
            final router = GoRouter.maybeOf(context);
            if (router != null && router.canPop()) {
              router.pop();
            } else {
              Navigator.maybeOf(context)?.maybePop();
            }
          },
          tooltip: 'Back',
        ),
        title: const Text('New expression'),
      ),
      body: async.when(
        loading: () => ConnectingView(deviceId: widget.lampId),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () =>
              ref.invalidate(controlNotifierProvider(widget.lampId)),
        ),
        data: (state) {
          // Same disconnect-handling pattern as ControlScreen: banner up
          // top, IgnorePointer + Opacity over the body so taps don't
          // queue against a dead BLE link.
          return Column(
            children: [
              if (!state.connected)
                ConnectionBanner(attempt: state.reconnectAttempt),
              Expanded(
                child: IgnorePointer(
                  ignoring: !state.connected,
                  child: Opacity(
                    opacity: state.connected ? 1.0 : 0.4,
                    child: _Body(
                      lampId: widget.lampId,
                      target: _target,
                      onTargetChanged: (t) => setState(() => _target = t),
                      existing: state.expressions.expressions,
                    ),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _Body extends StatelessWidget {
  const _Body({
    required this.lampId,
    required this.target,
    required this.onTargetChanged,
    required this.existing,
  });

  final String lampId;
  final int target;
  final ValueChanged<int> onTargetChanged;
  final List<ExpressionConfig> existing;

  bool _isTaken(String type, int t) =>
      existing.any((e) => e.type == type && e.target == t);

  bool _targetFull(int t) {
    // Disable a target only when every expression type already uses it.
    return ExpressionTypeMeta.all.every((m) => _isTaken(m.key, t));
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
      children: [
        const Padding(
          padding: EdgeInsets.only(top: 8, bottom: 8),
          child: Text(
            'Where should this expression play?',
            style: TextStyle(color: BrandColors.lampWhite, fontSize: 15),
          ),
        ),
        Row(
          children: [
            _TargetButton(
              label: 'Shade',
              icon: Icons.wb_incandescent_outlined,
              selected: target == 1,
              enabled: !_targetFull(1),
              onTap: () => onTargetChanged(1),
            ),
            const SizedBox(width: 8),
            _TargetButton(
              label: 'Base',
              icon: Icons.adjust,
              selected: target == 2,
              enabled: !_targetFull(2),
              onTap: () => onTargetChanged(2),
            ),
            const SizedBox(width: 8),
            _TargetButton(
              label: 'Both',
              icon: Icons.all_inclusive,
              selected: target == 3,
              enabled: !_targetFull(3),
              onTap: () => onTargetChanged(3),
            ),
          ],
        ),
        const SizedBox(height: 24),
        const Padding(
          padding: EdgeInsets.only(bottom: 8, left: 4),
          child: Text(
            'Pick an expression',
            style: TextStyle(color: BrandColors.lampWhite, fontSize: 15),
          ),
        ),
        for (final meta in ExpressionTypeMeta.all)
          _ExpressionCard(
            meta: meta,
            taken: _isTaken(meta.key, target),
            onTap: () {
              GoRouter.maybeOf(context)?.push(
                AppRoutes.expressionEditor(lampId, meta.key, target),
              );
            },
          ),
      ],
    );
  }
}

class _ExpressionCard extends StatelessWidget {
  const _ExpressionCard({
    required this.meta,
    required this.taken,
    required this.onTap,
  });

  final ExpressionTypeMeta meta;
  final bool taken;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final disabledOpacity = taken ? 0.35 : 1.0;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Material(
        color: BrandColors.lampWhite.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(12),
        child: InkWell(
          borderRadius: BorderRadius.circular(12),
          onTap: taken ? null : onTap,
          child: Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(12),
              border: Border.all(
                color: BrandColors.lampWhite.withValues(alpha: 0.06),
              ),
            ),
            child: Opacity(
              opacity: disabledOpacity,
              child: Row(
                children: [
                  Container(
                    width: 44,
                    height: 44,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: BrandColors.auroraBlue.withValues(alpha: 0.18),
                    ),
                    child: Icon(
                      meta.icon,
                      color: BrandColors.auroraBlue,
                    ),
                  ),
                  const SizedBox(width: 14),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Text(
                              meta.name,
                              style: const TextStyle(
                                color: BrandColors.lampWhite,
                                fontSize: 16,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                            if (taken) ...[
                              const SizedBox(width: 8),
                              Container(
                                padding: const EdgeInsets.symmetric(
                                    horizontal: 6, vertical: 2),
                                decoration: BoxDecoration(
                                  borderRadius: BorderRadius.circular(999),
                                  color: BrandColors.slateGrey
                                      .withValues(alpha: 0.2),
                                ),
                                child: const Text(
                                  'in use',
                                  style: TextStyle(
                                    fontSize: 10,
                                    color: BrandColors.slateGrey,
                                    fontWeight: FontWeight.w600,
                                  ),
                                ),
                              ),
                            ],
                          ],
                        ),
                        const SizedBox(height: 2),
                        Text(
                          meta.tagline,
                          style: const TextStyle(
                            color: BrandColors.fogGrey,
                            fontSize: 12,
                          ),
                        ),
                      ],
                    ),
                  ),
                  if (!taken)
                    const Icon(Icons.chevron_right,
                        color: BrandColors.slateGrey),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Big tappable target chooser. Bigger + iconned than a SegmentedButton
/// so the active target reads at a glance — important here since the
/// rest of the screen is a long list of expression cards and the user
/// needs to know "where am I about to drop this expression".
class _TargetButton extends StatelessWidget {
  const _TargetButton({
    required this.label,
    required this.icon,
    required this.selected,
    required this.enabled,
    required this.onTap,
  });

  final String label;
  final IconData icon;
  final bool selected;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final fill = selected ? BrandColors.glowPink : Colors.transparent;
    final border = selected
        ? BrandColors.glowPink
        : BrandColors.slateGrey.withValues(alpha: 0.5);
    final fg = !enabled
        ? BrandColors.slateGrey.withValues(alpha: 0.5)
        : (selected ? BrandColors.midnightBlack : BrandColors.lampWhite);
    return Expanded(
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: enabled ? onTap : null,
          borderRadius: BorderRadius.circular(12),
          child: Container(
            height: 72,
            decoration: BoxDecoration(
              color: fill,
              border: Border.all(
                color: border,
                width: selected ? 2 : 1,
              ),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, color: fg, size: 24),
                const SizedBox(height: 4),
                Text(
                  label,
                  style: TextStyle(
                    color: fg,
                    fontSize: 14,
                    fontWeight:
                        selected ? FontWeight.w700 : FontWeight.w500,
                    letterSpacing: 0.5,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
