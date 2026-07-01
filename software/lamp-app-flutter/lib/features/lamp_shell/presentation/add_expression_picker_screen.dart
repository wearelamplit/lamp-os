import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
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
    final textTheme = Theme.of(context).textTheme;
    return ListView(
      padding: const EdgeInsets.fromLTRB(
          AppSpace.lg, AppSpace.sm, AppSpace.lg, AppSpace.xl),
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
          child: Text(
            'Where should this expression play?',
            style: textTheme.bodyLarge,
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
            const SizedBox(width: AppSpace.sm),
            _TargetButton(
              label: 'Base',
              icon: Icons.adjust,
              selected: target == 2,
              enabled: !_targetFull(2),
              onTap: () => onTargetChanged(2),
            ),
            const SizedBox(width: AppSpace.sm),
            _TargetButton(
              label: 'Both',
              icon: Icons.all_inclusive,
              selected: target == 3,
              enabled: !_targetFull(3),
              onTap: () => onTargetChanged(3),
            ),
          ],
        ),
        const SizedBox(height: AppSpace.xl),
        Padding(
          padding: const EdgeInsets.only(bottom: AppSpace.sm, left: AppSpace.xs),
          child: Text(
            'Pick an expression',
            style: textTheme.bodyLarge,
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
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    final disabledOpacity = taken ? 0.35 : 1.0;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: AppSpace.sm),
      child: Material(
        color: colorScheme.surfaceContainer,
        borderRadius: BorderRadius.circular(AppRadius.card),
        child: InkWell(
          borderRadius: BorderRadius.circular(AppRadius.card),
          onTap: taken ? null : onTap,
          child: Container(
            padding: const EdgeInsets.all(AppSpace.lg),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(AppRadius.card),
              border: Border.all(color: colorScheme.outlineVariant),
            ),
            child: Opacity(
              opacity: disabledOpacity,
              child: Row(
                children: [
                  Container(
                    width: 44, // deliberate dimension, not spacing
                    height: 44,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: colorScheme.primaryContainer,
                    ),
                    child: Icon(meta.icon, color: colorScheme.onPrimaryContainer),
                  ),
                  const SizedBox(width: AppSpace.md),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Text(meta.name, style: textTheme.titleMedium),
                            if (taken) ...[
                              const SizedBox(width: AppSpace.sm),
                              Container(
                                padding: const EdgeInsets.symmetric(
                                    horizontal: AppSpace.sm, vertical: 2),
                                decoration: BoxDecoration(
                                  borderRadius: BorderRadius.circular(999), // pill shape, not spacing
                                  color: colorScheme.onSurfaceVariant
                                      .withValues(alpha: 0.2),
                                ),
                                child: Text(
                                  'in use',
                                  style: TextStyle(
                                    fontSize: 10,
                                    color: colorScheme.onSurfaceVariant,
                                    fontWeight: FontWeight.w600,
                                  ),
                                ),
                              ),
                            ],
                          ],
                        ),
                        const SizedBox(height: 2), // deliberate dimension, not spacing
                        Text(
                          meta.tagline,
                          style: textTheme.bodySmall?.copyWith(
                            color: colorScheme.onSurfaceVariant,
                          ),
                        ),
                      ],
                    ),
                  ),
                  if (!taken)
                    Icon(Icons.chevron_right,
                        color: colorScheme.onSurfaceVariant),
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
    final colorScheme = Theme.of(context).colorScheme;
    final fill = selected ? colorScheme.primary : Colors.transparent;
    final border = selected ? colorScheme.primary : colorScheme.outlineVariant;
    final fg = !enabled
        ? colorScheme.onSurfaceVariant
        : (selected ? colorScheme.onPrimary : colorScheme.onSurface);
    return Expanded(
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: enabled ? onTap : null,
          borderRadius: BorderRadius.circular(AppRadius.card),
          child: Container(
            height: 72,
            decoration: BoxDecoration(
              color: fill,
              border: Border.all(
                color: border,
                width: selected ? 2 : 1,
              ),
              borderRadius: BorderRadius.circular(AppRadius.card),
            ),
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(icon, color: fg, size: 24),
                const SizedBox(height: AppSpace.xs),
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
