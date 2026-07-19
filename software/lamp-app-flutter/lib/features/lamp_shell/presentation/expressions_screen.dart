import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/control_notifier.dart';
import '../../control/domain/sections.dart';
import '../../wisp/application/wisp_notifier.dart';
import '../domain/expression_catalog.dart';
import '../domain/expression_presentation.dart';

class ExpressionsScreen extends ConsumerWidget {
  const ExpressionsScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // .select to the expressions slice so brightness / shade / base slider
    // drags on ControlNotifier don't rebuild this whole list of rows; only
    // mutations to the expressions section (add / edit / reorder / delete /
    // enable toggle) do.
    final async = ref.watch(controlNotifierProvider(lampId).select(
      (a) => a.whenData((s) => s.expressions),
    ));
    // Gates the FAB below: disabled while the BLE link is mid-reconnect so
    // tapping it doesn't queue a write against a dead connection.
    final connected = ref.watch(controlNotifierProvider(lampId).select(
      (a) => a.value?.connected ?? true,
    ));
    final catalog = ref.watch(controlNotifierProvider(lampId)
        .select((a) => a.value?.catalog));
    return Scaffold(
      body: async.when(
        loading: () => const SizedBox.expand(),
        error: (e, _) => FriendlyError.page(
          title: "Couldn't reach your lamp.",
          subtitle:
              "They may have wandered out of range. Bring your phone closer "
              'and try again.',
          rawError: e,
          onRetry: () => ref.invalidate(controlNotifierProvider(lampId)),
        ),
        data: (exprSection) {
          final notifier =
              ref.read(controlNotifierProvider(lampId).notifier);
          final colorScheme = Theme.of(context).colorScheme;
          final Widget content = exprSection.expressions.isEmpty
              ? EmptyStatePane(
                  icon: Icon(
                    Icons.auto_awesome,
                    size: 56,
                    color: colorScheme.onSurfaceVariant,
                  ),
                  title: 'No expressions yet',
                  subtitle:
                      'Tap + to add a Glitch, Pulse, Breath or Shift effect.',
                )
              : Builder(builder: (context) {
                  _ExpressionTile tile(ExpressionConfig e) {
                    final descriptor = catalog?.byId(e.type);
                    final triggerable =
                        !effectiveContinuous(descriptor, e.parameters);
                    return _ExpressionTile(
                      lampId: lampId,
                      expression: e,
                      descriptor: descriptor,
                      title: descriptor?.name ??
                          (e.type.isEmpty ? '(unnamed)' : e.type),
                      onTrigger:
                          triggerable ? () => notifier.testExpression(e) : null,
                      onToggle: (v) async {
                        await notifier.upsertExpression(ExpressionConfig(
                          type: e.type,
                          enabled: v,
                          colors: e.colors,
                          intervalMin: e.intervalMin,
                          intervalMax: e.intervalMax,
                          target: e.target,
                          parameters: e.parameters,
                        ));
                      },
                      onConfirmDelete: () => _confirmDelete(context, e.type),
                      onDelete: () async {
                        await notifier.removeExpression(
                          type: e.type,
                          target: e.target,
                        );
                        if (!context.mounted) return;
                        AppSnackbar.action(
                          context,
                          message: 'Removed "${e.type}"',
                          actionLabel: 'UNDO',
                          onAction: () => notifier.upsertExpression(e),
                        );
                      },
                    );
                  }

                  final triggered = <ExpressionConfig>[];
                  final continuous = <ExpressionConfig>[];
                  for (final e in exprSection.expressions) {
                    (effectiveContinuous(catalog?.byId(e.type), e.parameters)
                            ? continuous
                            : triggered)
                        .add(e);
                  }
                  return ListView(
                    padding:
                        const EdgeInsets.symmetric(vertical: AppSpace.sm),
                    children: [
                      if (triggered.isNotEmpty) ...[
                        const SettingsGroupHeading('Triggered'),
                        ...triggered.map(tile),
                      ],
                      if (continuous.isNotEmpty) ...[
                        const SettingsGroupHeading('Continuous'),
                        ...continuous.map(tile),
                      ],
                    ],
                  );
                });
          return content;
        },
      ),
      floatingActionButton: async.hasValue
          ? FloatingActionButton(
              // Pushes the friendly target + type picker; the editor is
              // reached from there with a real `(type, target)` pair, not
              // the legacy `_new` sentinel. Disabled while the BLE link is
              // mid-reconnect: the picker would render but every write
              // from the editor would silently queue against a dead
              // connection. Hidden entirely when the notifier is in error
              // or loading state (no data to add expressions against).
              onPressed: connected
                  ? () => GoRouter.maybeOf(context)?.push(
                        AppRoutes.addExpression(lampId),
                      )
                  : null,
              backgroundColor: connected
                  ? null
                  : Theme.of(context).colorScheme.onSurfaceVariant,
              foregroundColor: Theme.of(context).colorScheme.onPrimary,
              tooltip: 'Add expression',
              child: const Icon(Icons.add),
            )
          : null,
    );
  }
}

/// Shows the confirmation dialog for deleting an expression. Returns `true`
/// if the user confirmed the delete.
Future<bool> _confirmDelete(BuildContext context, String type) async {
  final result = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: Text(type.isEmpty
          ? 'Delete this expression?'
          : 'Delete the "$type" expression?'),
      content: const Text(
        'You can undo this from the snackbar that appears after deleting.',
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('Cancel'),
        ),
        FilledButton(
          style: FilledButton.styleFrom(
            backgroundColor: Theme.of(ctx).colorScheme.error,
          ),
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Delete'),
        ),
      ],
    ),
  );
  return result ?? false;
}

class _ExpressionTile extends ConsumerStatefulWidget {
  const _ExpressionTile({
    required this.lampId,
    required this.expression,
    required this.descriptor,
    required this.title,
    required this.onTrigger,
    required this.onToggle,
    required this.onConfirmDelete,
    required this.onDelete,
  });

  final String lampId;
  final ExpressionConfig expression;
  final ExpressionDescriptor? descriptor;
  final String title;
  final VoidCallback? onTrigger;
  final ValueChanged<bool> onToggle;
  final Future<bool> Function() onConfirmDelete;
  final Future<void> Function() onDelete;

  @override
  ConsumerState<_ExpressionTile> createState() => _ExpressionTileState();
}

class _ExpressionTileState extends ConsumerState<_ExpressionTile> {
  /// Disables this row's ▶ for the fired expression's cycle duration so a
  /// transient plays out before it can be re-fired. Per-row, so rows cool
  /// down independently.
  Timer? _cooldownTimer;
  bool _cooling = false;

  @override
  void dispose() {
    _cooldownTimer?.cancel();
    super.dispose();
  }

  void _fire() {
    _cooldownTimer?.cancel();
    setState(() => _cooling = true);
    _cooldownTimer = Timer(
      triggerCooldown(widget.descriptor, widget.expression),
      () {
        if (mounted) setState(() => _cooling = false);
      },
    );
    widget.onTrigger?.call();
  }

  String get _targetLabel => switch (widget.expression.target) {
        1 => 'shade',
        2 => 'base',
        _ => 'both',
      };

  @override
  Widget build(BuildContext context) {
    final expression = widget.expression;
    final presentation = ExpressionPresentation.forId(expression.type);
    // Per-expression wisp-gating. Watch a tiny slice of wispStatus (just
    // `controlling`) so the row greys when a wisp takes over and un-greys
    // when the wisp releases.
    final wispControlling = ref.watch(
      wispNotifierProvider(widget.lampId).select(
        (async) => async.value?.controlling ?? false,
      ),
    );
    final muted =
        (widget.descriptor?.pausesWispOverride ?? false) && wispControlling;
    final connected = ref.watch(controlNotifierProvider(widget.lampId)
        .select((a) => a.value?.connected ?? false));
    final colorScheme = Theme.of(context).colorScheme;
    final textTheme = Theme.of(context).textTheme;
    return Dismissible(
      key: ValueKey('${expression.type}-${expression.target}'),
      direction: DismissDirection.endToStart,
      background: Container(
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.only(right: AppSpace.xl),
        color: colorScheme.error.withValues(alpha: 0.3),
        child: Icon(Icons.delete, color: colorScheme.error),
      ),
      confirmDismiss: (_) => widget.onConfirmDelete(),
      onDismissed: (_) => widget.onDelete(),
      child: Opacity(
        opacity: muted ? 0.35 : 1.0,
        child: InkWell(
          // `push` not `go`; see the FAB callsite for the same reason.
          onTap: muted
              ? null
              : () => GoRouter.maybeOf(context)?.push(
                    AppRoutes.expressionEditor(
                        widget.lampId, expression.type, expression.target),
                  ),
          child: Container(
            margin: const EdgeInsets.symmetric(
                horizontal: AppSpace.lg, vertical: AppSpace.xs),
            padding: const EdgeInsets.all(AppSpace.lg),
            decoration: BoxDecoration(
              color: colorScheme.surfaceContainer,
              borderRadius: BorderRadius.circular(AppRadius.card),
              border: Border.all(color: colorScheme.outlineVariant),
            ),
            child: Row(
              children: [
                Container(
                  width: 36, // deliberate dimension, not spacing
                  height: 36,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: colorScheme.onPrimaryContainer,
                  ),
                  child: Icon(presentation.icon,
                      size: 18, color: colorScheme.onPrimary),
                ),
                const SizedBox(width: AppSpace.md),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(widget.title, style: textTheme.titleMedium),
                      const SizedBox(height: AppSpace.xs),
                      Text(
                        _targetLabel,
                        style: textTheme.bodySmall?.copyWith(
                          color: colorScheme.onSurfaceVariant,
                        ),
                      ),
                    ],
                  ),
                ),
                if (widget.onTrigger != null)
                  IconButton(
                    icon: const Icon(Icons.play_arrow_rounded),
                    tooltip: 'Trigger',
                    onPressed:
                        (connected && !muted && !_cooling) ? _fire : null,
                  ),
                Switch(
                  value: expression.enabled,
                  onChanged: muted ? null : widget.onToggle,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
