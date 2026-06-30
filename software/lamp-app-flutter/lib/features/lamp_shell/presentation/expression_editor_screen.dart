import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/section_header.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/application/expression_draft.dart';
import '../../control/domain/lamp_color.dart';
import '../../control/domain/sections.dart';
import '../../control/presentation/widgets/color_picker_sheet.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/connection_banner.dart';
import '../../control/presentation/widgets/lamp_color_swatch.dart';
import '../../../core/widgets/interval_range_slider.dart';
import '../domain/expression_interval_math.dart';
import '../domain/expression_meta.dart';
import 'widgets/expression_params_panel.dart';

class ExpressionEditorScreen extends ConsumerStatefulWidget {
  const ExpressionEditorScreen({
    super.key,
    required this.lampId,
    required this.typeKey,
    required this.targetKey,
  });

  /// The expression type — locked. New entries reach this screen via the
  /// AddExpressionPickerScreen which chose the type up front; edits
  /// preserve whatever (type, target) the user tapped.
  final String typeKey;

  /// The expression target (1 = shade, 2 = base, 3 = both) — locked.
  final int targetKey;

  final String lampId;

  @override
  ConsumerState<ExpressionEditorScreen> createState() =>
      _ExpressionEditorScreenState();
}

class _ExpressionEditorScreenState
    extends ConsumerState<ExpressionEditorScreen> {
  /// Cached at initState so `dispose()` can call into the notifier without
  /// touching `ref` after the widget is deactivated (which Riverpod blocks).
  late final ControlNotifier _controlNotifier;

  /// Live target (1=shade, 2=base, 3=both). Seeded from the route's
  /// [ExpressionEditorScreen.targetKey] but mutable: the target switcher
  /// changes it in place (carrying the draft) instead of navigating, so the
  /// user doesn't lose in-progress edits. All draft reads key off this.
  late int _target;

  /// Whether the editor was opened on a not-yet-existing entry (i.e. reached
  /// via the picker, so the stack is [list, picker, editor]). Captured ONCE on
  /// first load, before any target switch — `isNew` is dynamic and flips true
  /// the moment you retarget, which must NOT change the back-out pop count.
  bool? _openedNew;

  bool _existsInState(ControlState? state) =>
      state != null &&
      state.expressions.expressions
          .any((e) => e.type == widget.typeKey && e.target == _target);

  @override
  void initState() {
    super.initState();
    _target = widget.targetKey;
    _controlNotifier =
        ref.read(controlNotifierProvider(widget.lampId).notifier);
  }

  @override
  void dispose() {
    // Tell the firmware to leave preview mode and re-enable the configurator
    // behaviors. Without this, a `test_expression` write performed while the
    // editor was open leaves the shade/base configurator stuck in
    // `disabled=true`, so subsequent shade/base color writes are received
    // by the lamp but never drawn.
    _controlNotifier.completeExpressionTest();
    super.dispose();
  }

  void _updateDraft(ExpressionConfig Function(ExpressionConfig d) f) {
    ref
        .read(expressionDraftProvider(
                widget.lampId, widget.typeKey, _target)
            .notifier)
        .update(f);
  }

  ExpressionConfig _withColors(ExpressionConfig d, List<LampColor> colors) =>
      d.copyWith(colors: colors);

  ExpressionConfig _withIntervals(ExpressionConfig d, int min, int max) =>
      d.copyWith(intervalMin: min, intervalMax: max);

  ExpressionConfig _withParameters(
          ExpressionConfig d, Map<String, int> p) =>
      d.copyWith(parameters: p);

  /// Open the color picker with live preview wired to the lamp. While
  /// the user drags R/G/B sliders, the picked color streams to
  /// whichever strips the expression's target paints (shade-only →
  /// shade, base-only → base, both → both) so the lamp preview matches
  /// what the saved expression will actually do. On close — confirm OR
  /// cancel — restore the lamp's main shade/base palette so the picker
  /// session doesn't permanently contaminate it. Returns the user's
  /// picked color, or null on cancel.
  Future<LampColor?> _pickColorLive(
    ControlState state,
    ControlNotifier notifier,
    LampColor initial,
  ) async {
    final t = _target;
    final previewShade = (t == 1 || t == 3);
    final previewBase = (t == 2 || t == 3);
    final originalShade = state.shade.colors;
    final originalBase = state.base.colors;

    // Stop any active expression test so the configurator is back in
    // charge and the picker's writes are visible. Tests disable the
    // configurator behavior, which would otherwise let the expression
    // keep painting over our live writes.
    unawaited(notifier.completeExpressionTest());

    void writeLive(LampColor c) {
      if (previewShade) unawaited(notifier.setShadeColor(c));
      if (previewBase) unawaited(notifier.setBaseColors([c]));
    }

    final picked = await showColorPickerSheet(
      context,
      initial: initial,
      onLive: writeLive,
    );

    // Restore whichever strips we drove regardless of confirm/cancel.
    // The picker's writes were for preview only; the expression's
    // palette is what should actually change (handled by the caller).
    if (previewShade && originalShade.isNotEmpty) {
      unawaited(notifier.setShadeColor(originalShade.first));
    }
    if (previewBase) {
      unawaited(notifier.setBaseColors(originalBase));
    }

    return picked;
  }

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(controlNotifierProvider(widget.lampId));
    final meta = ExpressionTypeMeta.byKey(widget.typeKey);
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
        // Title carries the expression's friendly name (titlized via
        // ExpressionTypeMeta) rather than the wire-level snake-case type.
        title: Text(_existsInState(async.value)
            ? (meta?.name ?? widget.typeKey)
            : 'New ${meta?.name ?? widget.typeKey}'),
        actions: [
          // Test: live-preview the editor's in-flight draft. We can't pass
          // colors/parameters in the test_expression envelope (the firmware
          // only takes type+target and triggers whatever is in its in-memory
          // expressionManager), so seed the draft via expressionOp first.
          // dispose() rolls it back if the user leaves without Saving.
          // Disabled when the BLE link is down — the writes would silently
          // queue against a dead connection.
          Consumer(
            builder: (context, ref, _) {
              final draft = ref.watch(expressionDraftProvider(
                  widget.lampId, widget.typeKey, _target));
              final connected = ref.watch(controlNotifierProvider(widget.lampId)
                  .select((a) => a.value?.connected ?? false));
              return IconButton(
                icon: const Icon(Icons.play_arrow_rounded),
                tooltip: 'Test',
                onPressed: !connected
                    ? null
                    : () async {
                        final notifier = ref.read(
                            controlNotifierProvider(widget.lampId).notifier);
                        // Persist via upsert first so testExpression triggers
                        // the configured entry (the lamp keys the trigger off
                        // the entry already in expressionManager).
                        await notifier.upsertExpression(draft);
                        await notifier.testExpression(draft);
                      },
              );
            },
          ),
        ],
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
          final notifier =
              ref.read(controlNotifierProvider(widget.lampId).notifier);
          final draft = ref.watch(expressionDraftProvider(
              widget.lampId, widget.typeKey, _target));
          final isNew = !_existsInState(state);
          // Capture the open-time newness once; retargeting flips `isNew`
          // but must not change how far we pop on back-out.
          _openedNew ??= isNew;

          // Two-row Column: scrolling form body up top, action row pinned
          // to the bottom inside a SafeArea so it stays above the system
          // gesture bar / nav bar on Android. Matches the layout pattern
          // used by knockout_screen and advanced_leds_screen.
          //
          // Wrapped in IgnorePointer + Opacity below when the BLE link is
          // down so the user can see the editor but can't fire writes
          // (Save / Test / color-pick) at a dead connection. Banner up
          // top mirrors the ControlScreen pattern.
          final body = Column(
            children: [
              Expanded(
                child: ListView(
                  padding: const EdgeInsets.all(AppSpace.lg),
                  children: [
                    _Header(meta: meta),
              const SizedBox(height: AppSpace.md),
              // Target switcher. Same chunky-pill UX as the picker so the
              // active target reads at a glance. Tapping a different target
              // retargets the current draft IN PLACE (no navigation), so the
              // user keeps their colors/interval/params; other-target buttons
              // are disabled when that combo is already configured (one entry
              // per (type, target) firmware-side).
              _TargetRow(
                currentTarget: _target,
                isTaken: (t) =>
                    t != _target &&
                    state.expressions.expressions.any((e) =>
                        e.type == widget.typeKey && e.target == t),
                onTap: (t) {
                  if (t == _target) return;
                  // Move the in-flight draft onto the new (type, t) slot and
                  // retarget it, then drop the old slot — the work follows the
                  // target instead of resetting or duplicating. isTaken
                  // guarantees the destination slot is a fresh default.
                  final from = _target;
                  final current = ref.read(expressionDraftProvider(
                      widget.lampId, widget.typeKey, from));
                  ref
                      .read(expressionDraftProvider(
                              widget.lampId, widget.typeKey, t)
                          .notifier)
                      .update((_) => current.copyWith(target: t));
                  ref.invalidate(expressionDraftProvider(
                      widget.lampId, widget.typeKey, from));
                  setState(() => _target = t);
                },
              ),
              const SizedBox(height: AppSpace.xl),

              // Colors
              const SectionHeader('Colors'),
              const SizedBox(height: AppSpace.sm),
              Wrap(
                spacing: AppSpace.sm,
                runSpacing: AppSpace.sm,
                children: [
                  for (var i = 0; i < draft.colors.length; i++)
                    _ColorChip(
                      color: draft.colors[i],
                      onEdit: () async {
                        final picked = await _pickColorLive(
                            state, notifier, draft.colors[i]);
                        if (picked == null) return;
                        final next = [...draft.colors];
                        next[i] = picked;
                        _updateDraft((d) => _withColors(d, next));
                      },
                      // Last swatch is non-removable so the palette can't
                      // end up empty (firmware would have nothing to draw).
                      onRemove: draft.colors.length > 1
                          ? () => _updateDraft((d) =>
                              _withColors(d, [...d.colors]..removeAt(i)))
                          : null,
                    ),
                  TextButton.icon(
                    icon: const Icon(Icons.add, size: 18),
                    label: const Text('Add color'),
                    onPressed: () async {
                      final picked = await _pickColorLive(
                          state,
                          notifier,
                          const LampColor(r: 0xFF, g: 0xFF, b: 0xFF, w: 0));
                      if (picked == null) return;
                      _updateDraft((d) => _withColors(d, [...d.colors, picked]));
                    },
                  ),
                ],
              ),
              const SizedBox(height: AppSpace.lg),

              // Hidden for breathing: continuous expressions ignore intervalMin/Max;
              // only breathSpeed (in the params panel) drives their timing.
              if (widget.typeKey != 'breathing') ...[
                const SectionHeader('Trigger interval'),
                IntervalRangeSlider(
                  values: RangeValues(
                    ExpressionIntervalMath.secToPos(draft.intervalMin),
                    ExpressionIntervalMath.secToPos(draft.intervalMax),
                  ),
                  min: ExpressionIntervalMath.secToPos(ExpressionIntervalMath.minSec),
                  max: ExpressionIntervalMath.secToPos(ExpressionIntervalMath.maxSec),
                  labelFor: (pos) => _fmtSeconds(ExpressionIntervalMath.posToSec(pos).toDouble()),
                  onChanged: (rv) {
                    final lo = ExpressionIntervalMath.posToSec(rv.start);
                    final hi = ExpressionIntervalMath.posToSec(rv.end);
                    _updateDraft((d) => _withIntervals(d, lo, hi));
                  },
                ),
                const SizedBox(height: AppSpace.lg),
              ],

              // Per-type parameters (replaces the old JSON text field).
              // devMode gates the cascade controls — those live behind
              // the persisted devMode flag (separate from session-only
              // advanced unlock so regular advanced users don't see the
              // power-user mesh fan-out toggles).
              ExpressionParamsPanel(
                type: draft.type,
                parameters: draft.parameters,
                devMode: ref.watch(
                  controlNotifierProvider(widget.lampId).select(
                      (async) => async.value?.lamp.devMode ?? false),
                ),
                onChanged: (p) => _updateDraft((d) => _withParameters(d, p)),
              ),
              // Wisp-override gate (`disabledDuringWispOverride`) is no
              // longer a user-facing toggle — the per-type default in
              // ExpressionTypeMeta.defaultDisabledDuringWispOverride is
              // authoritative (breathing + shifty pause during wisp
              // control; glitchy + pulse don't). The field is still
              // wired through the data model so a future custom-lamps
              // power-user surface can re-expose it; expressions_screen
              // continues to grey expressions whose default says they
              // pause during a wisp-active session.
              if (meta != null) ...[
                const SizedBox(height: AppSpace.xl),
                Text(
                  meta.description,
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                    fontStyle: FontStyle.italic,
                    height: 1.35,
                  ),
                ),
              ],
                  ],
                ),
              ),
              // Action row — Cancel + Delete on the left, Add/Update on
              // the right. Test sits on the AppBar's actions area; the
              // back-arrow on the AppBar leading area pops without
              // discarding the draft (so partial work survives a back
              // tap and re-open). Cancel is the explicit "throw away"
              // counterpart.
              //
              // This was previously the last child of the ListView and
              // scrolled with the form, leaving the buttons midway down
              // the screen on short forms. Pulling it out as a sibling
              // under SafeArea pins it to the bottom in line with the
              // rest of the editors. Bespoke shape (optional leading
              // Delete + dynamic primary label) so it doesn't share
              // EditorActionBar with the four uniform editors.
              SafeArea(
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                      horizontal: AppSpace.lg, vertical: AppSpace.sm),
                  child: Row(
                    children: [
                      TextButton.icon(
                        icon: const Icon(Icons.close, size: 18),
                        label: const Text('Cancel'),
                        onPressed: () {
                          // Discard the in-flight draft so re-opening
                          // shows the saved state, not the abandoned
                          // edits. dispose() handles firmware preview
                          ref
                              .read(expressionDraftProvider(widget.lampId,
                                      widget.typeKey, _target)
                                  .notifier)
                              .reset();
                          GoRouter.maybeOf(context)?.pop();
                        },
                      ),
                      if (!isNew)
                        TextButton.icon(
                          icon: const Icon(Icons.delete, size: 18),
                          label: const Text('Delete'),
                          style: TextButton.styleFrom(
                            foregroundColor:
                                Theme.of(context).colorScheme.error,
                          ),
                          onPressed: () async {
                            await notifier.removeExpression(
                              type: widget.typeKey,
                              target: _target,
                            );
                            ref
                                .read(expressionDraftProvider(widget.lampId,
                                        widget.typeKey, _target)
                                    .notifier)
                                .reset();
                            if (context.mounted) {
                              GoRouter.maybeOf(context)?.pop();
                            }
                          },
                        ),
                      const Spacer(),
                      FilledButton.icon(
                        icon: const Icon(Icons.check, size: 18),
                        label: const Text('Save'),
                        onPressed: () async {
                          // Capture the active target up front: the user can
                          // tap another target pill while the awaited writes
                          // are in flight, which would move _target out from
                          // under the reset() below.
                          final savedTarget = _target;
                          // Target changed this session → it's a move: drop
                          // the row we opened so it doesn't linger as a
                          // duplicate. removeExpression is a no-op when the
                          // original row never existed (the create flow).
                          if (savedTarget != widget.targetKey) {
                            await notifier.removeExpression(
                              type: widget.typeKey,
                              target: widget.targetKey,
                            );
                          }
                          await notifier.upsertExpression(draft);
                          ref
                              .read(expressionDraftProvider(widget.lampId,
                                      widget.typeKey, savedTarget)
                                  .notifier)
                              .reset();
                          if (!context.mounted) return;
                          // Entries opened via the picker have the stack
                          // [list, picker, editor]; pop twice to land back on
                          // the list, skipping the picker. Use the open-time
                          // flag, NOT live `isNew` — retargeting an existing
                          // entry flips `isNew` true but didn't add a picker
                          // to pop past.
                          final router = GoRouter.maybeOf(context);
                          if ((_openedNew ?? isNew) && router != null) {
                            router.pop();
                            if (context.mounted) router.pop();
                          } else {
                            router?.pop();
                          }
                        },
                      ),
                    ],
                  ),
                ),
              ),
            ],
          );
          return Column(
            children: [
              if (!state.connected)
                ConnectionBanner(attempt: state.reconnectAttempt),
              Expanded(
                child: IgnorePointer(
                  ignoring: !state.connected,
                  child: Opacity(
                    opacity: state.connected ? 1.0 : 0.4,
                    child: body,
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

class _Header extends StatelessWidget {
  const _Header({required this.meta});
  final ExpressionTypeMeta? meta;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Container(
      padding: const EdgeInsets.all(AppSpace.md),
      decoration: BoxDecoration(
        color: colorScheme.surfaceContainer,
        borderRadius: BorderRadius.circular(AppRadius.card),
        border: Border.all(color: colorScheme.outlineVariant),
      ),
      child: Row(
        children: [
          if (meta != null)
            Container(
              width: 40, // deliberate dimension, not spacing
              height: 40,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: colorScheme.primaryContainer,
              ),
              child: Icon(meta!.icon, color: colorScheme.primary),
            ),
          if (meta != null) const SizedBox(width: AppSpace.md),
          Expanded(
            child: Text(
              meta?.name ?? '(unknown)',
              style: Theme.of(context).textTheme.titleMedium,
            ),
          ),
        ],
      ),
    );
  }
}

/// Big chunky pill row: Shade / Base / Both. Same visual idiom as the
/// picker's target chooser (`add_expression_picker_screen.dart`) so the
/// active target reads at a glance. The current target shows as
/// selected and is a no-op on tap; other targets disable when this type
/// is already configured for them (firmware enforces one entry per
/// (type, target) pair, so tapping into an in-use combo would just
/// silently replace it — disable instead so it's obvious).
class _TargetRow extends StatelessWidget {
  const _TargetRow({
    required this.currentTarget,
    required this.isTaken,
    required this.onTap,
  });

  final int currentTarget;
  final bool Function(int t) isTaken;
  final ValueChanged<int> onTap;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        _TargetButton(
          label: 'Shade',
          icon: Icons.wb_incandescent_outlined,
          selected: currentTarget == 1,
          enabled: !isTaken(1),
          onTap: () => onTap(1),
        ),
        const SizedBox(width: AppSpace.sm),
        _TargetButton(
          label: 'Base',
          icon: Icons.adjust,
          selected: currentTarget == 2,
          enabled: !isTaken(2),
          onTap: () => onTap(2),
        ),
        const SizedBox(width: AppSpace.sm),
        _TargetButton(
          label: 'Both',
          icon: Icons.all_inclusive,
          selected: currentTarget == 3,
          enabled: !isTaken(3),
          onTap: () => onTap(3),
        ),
      ],
    );
  }
}

/// Local copy of the picker's `_TargetButton`. Kept duplicated (rather
/// than extracted) because the social-tab `_PersonalityButton` already
/// established the pattern of file-local pills, and the three usages
/// differ enough (3 vs 4 fields, different label semantics) that a
/// shared abstraction would carry more weight than it earns.
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
            height: 64,
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
                Icon(icon, color: fg, size: 22),
                const SizedBox(height: AppSpace.xs),
                Text(
                  label,
                  style: TextStyle(
                    color: fg,
                    fontSize: 13,
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

String _fmtSeconds(double seconds) {
  if (seconds < 90) return '${seconds.round()}s';
  final m = seconds / 60;
  if (m < 90) return '${m.round()}m';
  final h = m / 60;
  return '${h.toStringAsFixed(1).replaceAll(RegExp(r'\.0$'), '')}h';
}

class _ColorChip extends StatelessWidget {
  const _ColorChip({
    required this.color,
    required this.onEdit,
    required this.onRemove,
  });

  final LampColor color;
  final VoidCallback onEdit;

  /// Null when this swatch is the last one in the palette — the X button
  /// hides entirely so the user can't end up with an empty colors list.
  final VoidCallback? onRemove;

  @override
  Widget build(BuildContext context) {
    final colorScheme = Theme.of(context).colorScheme;
    return Stack(
      clipBehavior: Clip.none,
      children: [
        GestureDetector(
          onTap: onEdit,
          child: LampColorSwatch(color: color, size: 40),
        ),
        if (onRemove != null)
          Positioned(
            top: -6,
            right: -6,
            child: Semantics(
              label: 'Remove color',
              button: true,
              child: InkWell(
                onTap: onRemove,
                customBorder: const CircleBorder(),
                child: Container(
                  width: 18,
                  height: 18,
                  decoration: BoxDecoration(
                    color: colorScheme.surfaceContainerHigh,
                    shape: BoxShape.circle,
                  ),
                  child: Icon(
                    Icons.close,
                    size: 12,
                    color: colorScheme.onSurface,
                  ),
                ),
              ),
            ),
          ),
      ],
    );
  }
}
