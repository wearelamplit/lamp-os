import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/brand_colors.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/empty_state_pane.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/inactive_backdrop_scrim.dart';
import '../../control/application/control_notifier.dart';
import '../../control/domain/sections.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/connection_banner.dart';
import '../../wisp/application/wisp_notifier.dart';
import '../domain/expression_meta.dart';

class ExpressionsScreen extends ConsumerWidget {
  const ExpressionsScreen({super.key, required this.lampId});
  final String lampId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // .select to the expressions slice (audit perf-H2). The brightness /
    // shade / base slider drags on ControlNotifier no longer rebuild
    // this whole list of expression rows — only mutations to the
    // expressions section (add / edit / reorder / delete / enable
    // toggle) do.
    final async = ref.watch(controlNotifierProvider(lampId).select(
      (a) => a.whenData((s) => s.expressions),
    ));
    // Separate slice for the BLE link state so a mid-session disconnect
    // can grey out interaction + surface the reconnect banner. Same
    // pattern as ControlScreen, which is the canonical example.
    final connState = ref.watch(controlNotifierProvider(lampId).select(
      (a) => a.whenData(
          (s) => (connected: s.connected, attempt: s.reconnectAttempt)),
    ));
    final connected = connState.value?.connected ?? true;
    final attempt = connState.value?.attempt ?? 0;
    return Scaffold(
      body: async.when(
        loading: () => ConnectingView(deviceId: lampId),
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
          final Widget content = exprSection.expressions.isEmpty
              ? const EmptyStatePane(
                  icon: Icon(
                    Icons.auto_awesome,
                    size: 56,
                    color: BrandColors.slateGrey,
                  ),
                  title: 'No expressions yet',
                  subtitle:
                      'Tap + to add a Glitch, Pulse, Breath or Shift effect.',
                )
              : ListView.builder(
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  itemCount: exprSection.expressions.length,
                  itemBuilder: (ctx, i) {
                    final e = exprSection.expressions[i];
                    return _ExpressionTile(
                      lampId: lampId,
                      expression: e,
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
                      onConfirmDelete: () =>
                          _confirmDelete(context, e.type),
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
                      onTrigger: () => notifier.testExpression(e),
                    );
                  },
                );
          return Column(
            children: [
              if (!connected) ConnectionBanner(attempt: attempt),
              Expanded(
                child: IgnorePointer(
                  ignoring: !connected,
                  child: Opacity(
                    opacity: connected ? 1.0 : 0.4,
                    child: content,
                  ),
                ),
              ),
            ],
          );
        },
      ),
      floatingActionButton: async.hasValue
          ? FloatingActionButton(
              // Pushes the friendly target + type picker; the editor is
              // reached from there with a real `(type, target)` pair, not
              // the legacy `_new` sentinel. Disabled while the BLE link is
              // mid-reconnect — the picker would render but every write
              // from the editor would silently queue against a dead
              // connection. Hidden entirely when the notifier is in error
              // or loading state (no data to add expressions against).
              onPressed: connected
                  ? () => GoRouter.maybeOf(context)?.push(
                        AppRoutes.addExpression(lampId),
                      )
                  : null,
              backgroundColor: connected ? null : BrandColors.slateGrey,
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
  final result = await showBlurredDialog<bool>(
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
          style: FilledButton.styleFrom(backgroundColor: Colors.redAccent),
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('Delete'),
        ),
      ],
    ),
  );
  return result ?? false;
}

class _ExpressionTile extends ConsumerWidget {
  const _ExpressionTile({
    required this.lampId,
    required this.expression,
    required this.onToggle,
    required this.onConfirmDelete,
    required this.onDelete,
    required this.onTrigger,
  });

  final String lampId;
  final ExpressionConfig expression;
  final ValueChanged<bool> onToggle;
  final Future<bool> Function() onConfirmDelete;
  final Future<void> Function() onDelete;
  final VoidCallback onTrigger;

  String get _targetLabel => switch (expression.target) {
        1 => 'shade',
        2 => 'base',
        _ => 'both',
      };

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final meta = ExpressionTypeMeta.byKey(expression.type);
    final title = meta?.name ??
        (expression.type.isEmpty ? '(unnamed)' : expression.type);
    // Per-expression wisp-gating. Wisp-disabled-ness is a type-property
    // (refactor 2026-06-13 — removed the per-instance toggle), so we look
    // it up via ExpressionTypeMeta. Watch a tiny slice of wispStatus
    // (just `controlling`) so the row greys when a wisp takes over and
    // un-greys when the wisp releases.
    final wispControlling = ref.watch(
      wispNotifierProvider(lampId).select(
        (async) => async.value?.controlling ?? false,
      ),
    );
    final muted =
        (meta?.defaultDisabledDuringWispOverride ?? false) && wispControlling;
    return Dismissible(
      key: ValueKey('${expression.type}-${expression.target}'),
      direction: DismissDirection.endToStart,
      background: Container(
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.only(right: 24),
        color: Colors.redAccent.withValues(alpha: 0.3),
        child: const Icon(Icons.delete, color: Colors.redAccent),
      ),
      confirmDismiss: (_) => onConfirmDelete(),
      onDismissed: (_) => onDelete(),
      child: Opacity(
        opacity: muted ? 0.35 : 1.0,
        child: InkWell(
          // `push` not `go` — see the FAB callsite for the same reason.
          onTap: muted
              ? null
              : () => GoRouter.maybeOf(context)?.push(
                    AppRoutes.expressionEditor(
                        lampId, expression.type, expression.target),
                  ),
          child: Container(
            margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Colors.white.withValues(alpha: 0.04),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
            ),
            child: Row(
              children: [
                Container(
                  width: 36,
                  height: 36,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: BrandColors.auroraBlue.withValues(alpha: 0.16),
                  ),
                  child: Icon(meta?.icon ?? Icons.auto_awesome,
                      size: 18, color: BrandColors.auroraBlue),
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title,
                        style: const TextStyle(
                          color: BrandColors.lampWhite,
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        _targetLabel,
                        style: const TextStyle(
                          color: BrandColors.fogGrey,
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ),
                ),
                IconButton(
                  tooltip: 'Trigger now',
                  icon: const Icon(Icons.play_arrow,
                      color: BrandColors.lumenGreen),
                  onPressed: muted ? null : onTrigger,
                ),
                Switch(
                  value: expression.enabled,
                  onChanged: muted ? null : onToggle,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
