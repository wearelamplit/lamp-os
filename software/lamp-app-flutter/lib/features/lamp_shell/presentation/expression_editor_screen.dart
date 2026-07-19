import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/ble/ble_client.dart';
import '../../../core/ble/ble_client_provider.dart';
import '../../../core/ble/uuids.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/confirm_discard.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/lamp_card.dart';
import '../../../core/widgets/settings_row.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/application/dev_mode.dart';
import '../../control/application/expression_draft.dart';
import '../../control/domain/lamp_color.dart';
import '../../control/domain/sections.dart';
import '../../control/presentation/widgets/color_blocks_bar.dart';
import '../../control/presentation/widgets/color_stops_sheet.dart';
import '../domain/expression_catalog.dart';
import '../domain/expression_presentation.dart';
import 'widgets/expression_params_panel.dart';

class ExpressionEditorScreen extends ConsumerStatefulWidget {
  const ExpressionEditorScreen({
    super.key,
    required this.lampId,
    required this.typeKey,
    required this.targetKey,
  });

  /// The expression type, locked. New entries reach this screen via the
  /// AddExpressionPickerScreen which chose the type up front; edits
  /// preserve whatever (type, target) the user tapped.
  final String typeKey;

  /// The expression target (1 = shade, 2 = base, 3 = both), locked.
  final int targetKey;

  final String lampId;

  @override
  ConsumerState<ExpressionEditorScreen> createState() =>
      _ExpressionEditorScreenState();
}

class _ExpressionEditorScreenState
    extends ConsumerState<ExpressionEditorScreen> {
  /// Captured at initState so `dispose()` can fire the test-complete write
  /// without touching `ref` (blocked once deactivated) or a ControlNotifier
  /// that a mid-session `ref.invalidate` may have staled out. BleClient lives
  /// in a keepAlive provider, so the reference stays valid for the app's life.
  late final BleClient _ble;

  /// Live target (1=shade, 2=base, 3=both). Seeded from the route's
  /// [ExpressionEditorScreen.targetKey] but mutable: the target switcher
  /// changes it in place (carrying the draft) instead of navigating, so the
  /// user doesn't lose in-progress edits. All draft reads key off this.
  late int _target;

  /// Whether the editor was opened on a not-yet-existing entry (i.e. reached
  /// via the picker, so the stack is [list, picker, editor]). Captured ONCE on
  /// first load, before any target switch. `isNew` is dynamic and flips true
  /// the moment you retarget, which must NOT change the back-out pop count.
  bool? _openedNew;

  /// True once the user has touched the draft this session (color / interval /
  /// param edit or a target switch). Gates the discard guard: a clean back-out
  /// pops straight through, a dirty one confirms first.
  bool _dirty = false;

  /// Disables the Test ▶ for the fired expression's cycle duration so a
  /// transient plays out before it can be re-fired.
  Timer? _cooldownTimer;
  bool _cooling = false;

  bool _existsInState(ControlState? state) =>
      state != null &&
      state.expressions.expressions
          .any((e) => e.type == widget.typeKey && e.target == _target);

  @override
  void initState() {
    super.initState();
    _target = widget.targetKey;
    _ble = ref.read(bleClientProvider);
  }

  @override
  void dispose() {
    _cooldownTimer?.cancel();
    // Tell the firmware to leave preview mode and re-enable the configurator
    // behaviors. Without this, a `test_expression` write performed while the
    // editor was open leaves the shade/base configurator stuck in
    // `disabled=true`, so subsequent shade/base color writes are received
    // by the lamp but never drawn. Fire-and-forget straight through the
    // BleClient: dispose can't await and the notifier may be staled out.
    unawaited(_ble
        .write(
          widget.lampId,
          BleUuids.controlService,
          BleUuids.expressionTest,
          Uint8List.fromList(utf8.encode('{"a":"test_expression_complete"}')),
        )
        .catchError((Object _) {}));
    super.dispose();
  }

  void _startCooldown(Duration d) {
    _cooldownTimer?.cancel();
    setState(() => _cooling = true);
    _cooldownTimer = Timer(d, () {
      if (mounted) setState(() => _cooling = false);
    });
  }

  void _updateDraft(ExpressionConfig Function(ExpressionConfig d) f) {
    _dirty = true;
    ref
        .read(expressionDraftProvider(
                widget.lampId, widget.typeKey, _target)
            .notifier)
        .update(f);
  }

  void _resetDraft() {
    ref
        .read(expressionDraftProvider(widget.lampId, widget.typeKey, _target)
            .notifier)
        .reset();
  }

  void _pop() {
    final router = GoRouter.maybeOf(context);
    if (router != null && router.canPop()) {
      router.pop();
    } else {
      Navigator.maybeOf(context)?.maybePop();
    }
  }

  /// Back-out handler for the AppBar arrow and system back. Confirms before
  /// discarding when the draft has unsaved edits; a clean draft pops straight
  /// through. Reuses the base/shade editors' [confirmDiscard] dialog.
  Future<void> _leaveGuarded() async {
    if (_dirty) {
      final discard = await confirmDiscard(context);
      if (!discard || !mounted) return;
      _resetDraft();
    }
    if (mounted) _pop();
  }

  ExpressionConfig _withColors(ExpressionConfig d, List<LampColor> colors) =>
      d.copyWith(colors: colors);

  ExpressionConfig _withIntervals(ExpressionConfig d, int min, int max) =>
      d.copyWith(intervalMin: min, intervalMax: max);

  ExpressionConfig _withParameters(
          ExpressionConfig d, Map<String, int> p) =>
      d.copyWith(parameters: p);

  /// Open the color-stops sheet with live preview wired to the lamp. Every
  /// in-flight edit streams the whole stop list to whichever strips the
  /// expression's target paints (shade-only → shade, base-only → base, both →
  /// both) so the lamp preview matches the saved expression. An empty list
  /// means "follow surface", so it isn't streamed (streaming empty would blank
  /// the strip). On close (save or cancel) the surface's original palette is
  /// restored: the preview writes were for preview only; the expression's own
  /// colors are committed by Save via `upsertExpression`.
  Future<void> _editColorsLive(
    ControlState state,
    ControlNotifier notifier,
    List<LampColor> initial,
    int colorMax,
    bool canEmpty,
  ) async {
    final t = _target;
    final previewShade = (t == 1 || t == 3);
    final previewBase = (t == 2 || t == 3);
    final originalShade = state.shade.colors;
    final originalBase = state.base.colors;

    // Stop any active expression test so the configurator is back in charge
    // and the preview writes are visible.
    unawaited(notifier.completeExpressionTest());

    void writeLive(List<LampColor> colors) {
      if (colors.isEmpty) return;
      if (previewShade) unawaited(notifier.setShadeColors(colors));
      if (previewBase) unawaited(notifier.setBaseColors(colors));
    }

    await showColorStopsSheet(
      context,
      initial: initial,
      title: 'Colors',
      description: 'This effect picks colors from this set at random.',
      max: colorMax,
      allowEmpty: canEmpty,
      reorderable: false,
      onChanged: (colors) {
        _updateDraft((d) => _withColors(d, colors));
        writeLive(colors);
      },
      onSave: (colors) async => _updateDraft((d) => _withColors(d, colors)),
    );

    // Restore whichever strips were driven, regardless of save/cancel.
    if (previewShade) unawaited(notifier.setShadeColors(originalShade));
    if (previewBase) unawaited(notifier.setBaseColors(originalBase));
  }

  /// Pixel count for the active target's strip; max of shade/base for `both`.
  int _pixelCount(ControlState state) => _target == 1
      ? state.shade.px
      : _target == 2
          ? state.base.px
          : (state.shade.px > state.base.px ? state.shade.px : state.base.px);

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(controlNotifierProvider(widget.lampId));
    final descriptor = async.value?.catalog?.byId(widget.typeKey);
    final name = descriptor?.name ?? widget.typeKey;
    final icon = ExpressionPresentation.forId(widget.typeKey).icon;
    return PopScope(
      canPop: !_dirty,
      onPopInvokedWithResult: (didPop, _) async {
        if (didPop) return; // clean draft, already popped
        final discard = await confirmDiscard(context);
        if (!discard || !mounted) return;
        _resetDraft();
        if (mounted) _pop();
      },
      child: Scaffold(
      appBar: AppBar(
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: _leaveGuarded,
          tooltip: 'Back',
        ),
        // Title carries the catalog's friendly name rather than the
        // wire-level snake-case type.
        title: Text(_existsInState(async.value) ? name : 'New $name'),
        actions: [
          // Test previews the in-flight draft transiently (no persist); the
          // test_expression envelope carries colors + params. A continuous
          // expression plays one cycle. Disabled while the BLE link is down so
          // writes don't queue against a dead connection.
          if (descriptor != null)
          Consumer(
            builder: (context, ref, _) {
              final connected = ref.watch(controlNotifierProvider(widget.lampId)
                  .select((a) => a.value?.connected ?? false));
              return IconButton(
                icon: const Icon(Icons.play_arrow_rounded),
                tooltip: 'Test',
                // Read the draft on tap, not on build: watching it here would
                // force the keep-alive draft to seed during the loading frame,
                // before the catalog + pixel counts have landed.
                onPressed: (!connected || _cooling)
                    ? null
                    : () async {
                        final draft = ref.read(expressionDraftProvider(
                            widget.lampId, widget.typeKey, _target));
                        final notifier = ref.read(
                            controlNotifierProvider(widget.lampId).notifier);
                        _startCooldown(triggerCooldown(descriptor, draft));
                        await notifier.testExpression(draft);
                      },
              );
            },
          ),
        ],
      ),
      body: async.when(
        loading: () => const SizedBox.expand(),
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
          // but must not change how far the back-out pops.
          _openedNew ??= isNew;

          // Surfaces this expression can't target, from the catalog. Both
          // (target 3) is excluded whenever either surface is.
          final excludedTargets = <int>{};
          if (descriptor != null) {
            if (descriptor.excludeTargets.contains('shade')) {
              excludedTargets.addAll({1, 3});
            }
            if (descriptor.excludeTargets.contains('base')) {
              excludedTargets.addAll({2, 3});
            }
          }
          final colorMax = descriptor?.colors.max ?? 8;
          // inheritsSurface makes an empty palette valid ("follow surface").
          final canEmpty = descriptor?.colors.inheritsSurface ?? false;

          // Two-row Column: scrolling form body up top, action row pinned
          // to the bottom inside a SafeArea so it stays above the system
          // gesture bar / nav bar on Android. Matches the layout pattern
          // used by knockout_screen and advanced_leds_screen.
          return Column(
            children: [
              Expanded(
                child: ListView(
                  padding: const EdgeInsets.all(AppSpace.lg),
                  children: [
                    _Header(icon: icon, name: name),
                    const SettingsGroupHeading('Target'),
                    // Same chunky-pill UX as the picker so the active target
                    // reads at a glance. Tapping a different target retargets
                    // the current draft IN PLACE (no navigation), so the user
                    // keeps their colors/interval/params; other-target buttons
                    // disable when that combo is already configured (one entry
                    // per (type, target) firmware-side).
                    LampCard(
                      child: _TargetRow(
                        currentTarget: _target,
                        isTaken: (t) =>
                            excludedTargets.contains(t) ||
                            (t != _target &&
                                t != widget.targetKey &&
                                state.expressions.expressions.any((e) =>
                                    e.type == widget.typeKey && e.target == t)),
                        onTap: (t) {
                          if (t == _target) return;
                          // Move the in-flight draft onto the new (type, t) slot
                          // and retarget it, then drop the old slot: the work
                          // follows the target instead of resetting or
                          // duplicating. isTaken guarantees the destination slot
                          // is a fresh default.
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
                          setState(() {
                            _target = t;
                            _dirty = true;
                          });
                        },
                      ),
                    ),
                    // Placement (zone) sits directly under Target, above
                    // Colors, so the effect's extent reads before its palette.
                    if (descriptor != null)
                      ExpressionParamsPanel(
                        descriptor: descriptor,
                        part: ExpressionPanelPart.placement,
                        parameters: draft.parameters,
                        intervalMin: draft.intervalMin,
                        intervalMax: draft.intervalMax,
                        onIntervalChanged: (lo, hi) =>
                            _updateDraft((d) => _withIntervals(d, lo, hi)),
                        pixelCount: _pixelCount(state),
                        devMode: ref.watch(devModeOnProvider),
                        onChanged: (p) =>
                            _updateDraft((d) => _withParameters(d, p)),
                        onZonePreview: (lo, hi) {
                          if (draft.colors.isEmpty) return;
                          notifier.previewZoneHighlight(
                              lo, hi, _target, draft.colors.first);
                        },
                        onZonePreviewEnd: () =>
                            unawaited(notifier.completeExpressionTest()),
                      ),
                    const SettingsGroupHeading('Colors'),
                    ColorBlocksBar(
                      colors: draft.colors,
                      onTap: () => _editColorsLive(
                          state, notifier, draft.colors, colorMax, canEmpty),
                    ),

                    // Timing / Behaviour / Mesh render generically from the
                    // firmware catalog descriptor into their own yellow-headed
                    // cards. Colors + target above are the editor shell's own
                    // concern.
                    if (descriptor != null)
                      ExpressionParamsPanel(
                        descriptor: descriptor,
                        parameters: draft.parameters,
                        intervalMin: draft.intervalMin,
                        intervalMax: draft.intervalMax,
                        onIntervalChanged: (lo, hi) =>
                            _updateDraft((d) => _withIntervals(d, lo, hi)),
                        pixelCount: _pixelCount(state),
                        devMode: ref.watch(devModeOnProvider),
                        onChanged: (p) =>
                            _updateDraft((d) => _withParameters(d, p)),
                      ),
                  ],
                ),
              ),
              // Action row: Cancel + Delete on the left, Add/Update on
              // the right. Test sits on the AppBar's actions area; the
              // back-arrow on the AppBar leading area (and system back)
              // confirm before discarding when the draft has unsaved
              // edits. Cancel is the explicit "throw away" counterpart:
              // it discards without a prompt.
              //
              // A sibling under SafeArea pins the row to the bottom in
              // line with the rest of the editors, above the system
              // gesture bar / nav bar on Android. Bespoke shape (optional
              // leading Delete + dynamic primary label) so it doesn't
              // share EditorActionBar with the four uniform editors.
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
                            final target = _target;
                            // Capture the keep-alive draft notifier before the
                            // await so the reset doesn't touch `ref` after the
                            // widget may have unmounted mid-write.
                            final draftNotifier = ref.read(
                                expressionDraftProvider(widget.lampId,
                                        widget.typeKey, target)
                                    .notifier);
                            await notifier.removeExpression(
                              type: widget.typeKey,
                              target: target,
                            );
                            draftNotifier.reset();
                            if (context.mounted) {
                              GoRouter.maybeOf(context)?.pop();
                            }
                          },
                        ),
                      const Spacer(),
                      FilledButton.icon(
                        icon: const Icon(Icons.check, size: 18),
                        label: const Text('Save'),
                        style: FilledButton.styleFrom(
                          shape: RoundedRectangleBorder(
                            borderRadius:
                                BorderRadius.circular(AppRadius.card),
                          ),
                        ),
                        onPressed: () async {
                          // Capture the active target up front: the user can
                          // tap another target pill while the awaited writes
                          // are in flight, which would move _target out from
                          // under the reset() below.
                          final savedTarget = _target;
                          // Capture the keep-alive draft notifier before the
                          // awaits so the reset doesn't touch `ref` after the
                          // widget may have unmounted mid-write.
                          final draftNotifier = ref.read(
                              expressionDraftProvider(widget.lampId,
                                      widget.typeKey, savedTarget)
                                  .notifier);
                          // A moved target leaves the origin row behind;
                          // remove it. No-op when the origin never existed.
                          if (savedTarget != widget.targetKey) {
                            await notifier.removeExpression(
                              type: widget.typeKey,
                              target: widget.targetKey,
                            );
                          }
                          await notifier.upsertExpression(draft);
                          draftNotifier.reset();
                          if (!context.mounted) return;
                          // Entries opened via the picker have the stack
                          // [list, picker, editor]; pop twice to land back on
                          // the list, skipping the picker. Use the open-time
                          // flag, not live `isNew`: retargeting an existing
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
        },
      ),
    ),
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({required this.icon, required this.name});
  final IconData icon;
  final String name;

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
          Container(
            width: 40, // deliberate dimension, not spacing
            height: 40,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: colorScheme.onPrimaryContainer,
            ),
            child: Icon(icon, color: colorScheme.onPrimary),
          ),
          const SizedBox(width: AppSpace.md),
          Expanded(
            child: Text(
              name,
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
/// silently replace it; disable instead so it's obvious).
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
    final fg = selected ? colorScheme.onPrimary : colorScheme.onSurface;
    return Expanded(
      child: Opacity(
        opacity: enabled ? 1.0 : 0.35,
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
      ),
    );
  }
}

