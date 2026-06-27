import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../inventory/application/inventory_notifier.dart';
import '../application/control_notifier.dart';
import 'widgets/connecting_view.dart';
import 'widgets/disconnect_aware_body.dart';

/// Per-pixel knockout editor with a region-split gesture model:
/// - Touch in the **left+middle zone** (label + bar): paint. y picks
///   the row, x picks the brightness, continuously, as the finger
///   moves — one stroke can paint many rows.
/// - Touch in the **right "%" readout zone**: vertical drag scrolls
///   the list. Used when pixelCount × rowHeight overflows the
///   viewport.
///
/// Splitting by spatial region avoids the mode/toggle that a single
/// gesture model would have required.
class KnockoutScreen extends ConsumerStatefulWidget {
  const KnockoutScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<KnockoutScreen> createState() => _KnockoutScreenState();
}

class _KnockoutScreenState extends ConsumerState<KnockoutScreen> {
  /// Session baseline. First non-null state paint snapshots the current
  /// knockout map; Cancel diffs against this and re-writes each pixel that
  /// drifted (re-setting added pixels to 100 = default = "no knockout").
  Map<int, int>? _original;

  /// Used to read the inner column's RenderBox so we can translate global
  /// pointer events into local (x, y) for row+value math.
  final GlobalKey _columnKey = GlobalKey();

  /// Used to read the viewport (the SingleChildScrollView) for x-zone math
  /// (the readout strip is at the right edge of the viewport, not the
  /// inner column which scrolls).
  final GlobalKey _viewportKey = GlobalKey();

  /// We drive scrolling ourselves so the readout-zone gesture can move
  /// the list without competing with the paint zone's pointer events.
  final ScrollController _scrollCtrl = ScrollController();

  /// Row layout constants — must match `_PixelBar`.
  static const double _rowHeight = 28.0;
  static const double _hPadding = 16.0;
  static const double _labelWidth = 32.0;
  static const double _readoutWidth = 40.0;

  /// Horizontal inset between the column's bar-painting band and the
  /// visible bar. Gives the user a `_barInsetH`-wide grab zone at each
  /// end that snaps to 0 % / 100 % so a finger drifting toward the
  /// scrollable readout strip still lands in paint territory. Without
  /// this the visible bar ran flush against the readout zone and the
  /// last few percent were nearly impossible to set.
  static const double _barInsetH = 12.0;

  @override
  void dispose() {
    _scrollCtrl.dispose();
    super.dispose();
  }

  void _cancel() {
    final orig = _original;
    if (orig == null) {
      Navigator.of(context).pop();
      return;
    }
    final notifier =
        ref.read(controlNotifierProvider(widget.lampId).notifier);
    final cur = ref.read(controlNotifierProvider(widget.lampId)).value;
    final currentMap = cur?.base.knockout ?? const <int, int>{};
    final pixels = <int>{...orig.keys, ...currentMap.keys};
    for (final p in pixels) {
      final origVal = orig[p] ?? 100;
      if (currentMap[p] != origVal) {
        notifier.setKnockoutPixel(p, origVal);
      }
    }
    Navigator.of(context).pop();
  }

  /// True when the pointer's viewport-local x lands inside the right-side
  /// readout strip (the `100%` text column). That's the scroll zone.
  bool _inScrollZone(double localX, double viewportWidth) {
    return localX >= viewportWidth - _hPadding - _readoutWidth;
  }

  /// Translate a pointer position (in the inner column's local space)
  /// into (rowIdx, value) and apply via the notifier. Internal
  /// per-pixel 30 ms debounce in `setKnockoutPixel` coalesces redundant
  /// calls firmware-side.
  void _paintAt(Offset colLocal, int pixelCount, ControlNotifier notifier) {
    final box = _columnKey.currentContext?.findRenderObject() as RenderBox?;
    if (box == null) return;
    final colWidth = box.size.width;

    // The paint band is the full column space between the label and the
    // readout strip. The *visible* bar inside that band is inset by
    // `_barInsetH` on each side; the inset region maps to 0 % / 100 %
    // via the .clamp() below, giving an extra easy-grab zone at each
    // end without overlapping the right-side scroll strip.
    const paintLeft = _hPadding + _labelWidth;
    final paintRight = colWidth - _hPadding - _readoutWidth;
    const barLeft = paintLeft + _barInsetH;
    final barRight = paintRight - _barInsetH;
    final barWidth = barRight - barLeft;
    if (barWidth <= 0) return;

    final rowIdx = (colLocal.dy ~/ _rowHeight).clamp(0, pixelCount - 1);
    final barX = (colLocal.dx - barLeft).clamp(0.0, barWidth);
    final value = (barX / barWidth * 100).round().clamp(0, 100);

    notifier.setKnockoutPixel(rowIdx, value);
  }

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(controlNotifierProvider(widget.lampId));
    final inv = ref.watch(inventoryNotifierProvider).value;
    final name = inv
            ?.firstWhereOrNull((l) => l.id == widget.lampId)
            ?.name ??
        widget.lampId;

    // Knockout count is sourced from the same async state the body renders.
    // Surfaced in the AppBar's actions slot (where a check/save button would
    // normally sit) to match other editor screens' "indicator top-right,
    // action row bottom" pattern.
    final knockoutCount = async.value?.base.knockout.length ?? 0;
    return PopScope(
      canPop: true,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop) {
          ref
              .read(controlNotifierProvider(widget.lampId).notifier)
              .flushKnockoutCommit();
        }
      },
      child: Scaffold(
        appBar: AppBar(
          leading: const BackButtonLeading(),
          title: Text('Pixel Knockout · $name'),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 16),
              child: Center(
                child: Text(
                  '$knockoutCount edited',
                  style: TextStyle(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                    fontSize: 12,
                  ),
                ),
              ),
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
            _original ??= Map.of(state.base.knockout);
            final notifier =
                ref.read(controlNotifierProvider(widget.lampId).notifier);
            final pixelCount = state.base.px;

            void handle(PointerEvent event) {
              final vp = _viewportKey.currentContext?.findRenderObject()
                  as RenderBox?;
              final col = _columnKey.currentContext?.findRenderObject()
                  as RenderBox?;
              if (vp == null || col == null) return;
              final vpLocal = vp.globalToLocal(event.position);

              if (_inScrollZone(vpLocal.dx, vp.size.width)) {
                // Scroll zone: drag y → scroll. PointerDown does nothing
                // (don't want a tap to nudge the list); only PointerMove
                // scrolls by the delta.
                if (event is PointerMoveEvent && _scrollCtrl.hasClients) {
                  final next = (_scrollCtrl.offset - event.delta.dy)
                      .clamp(0.0, _scrollCtrl.position.maxScrollExtent);
                  _scrollCtrl.jumpTo(next);
                }
                return;
              }

              // Paint zone: translate to the inner column's space so the
              // row index calculation is stable across any scroll offset.
              final colLocal = col.globalToLocal(event.position);
              _paintAt(colLocal, pixelCount, notifier);
            }

            return DisconnectAwareBody(
              lampId: widget.lampId,
              child: Column(
              children: [
                Expanded(
                  child: Listener(
                    behavior: HitTestBehavior.opaque,
                    onPointerDown: handle,
                    onPointerMove: handle,
                    child: SingleChildScrollView(
                      key: _viewportKey,
                      controller: _scrollCtrl,
                      physics: const NeverScrollableScrollPhysics(),
                      child: Column(
                        key: _columnKey,
                        children: [
                          for (var i = 0; i < pixelCount; i++)
                            _PixelBar(lampId: widget.lampId, index: i),
                        ],
                      ),
                    ),
                  ),
                ),
                // Action row — Cancel (discards live-applied changes by
                // rewriting every drifted pixel back to its original value),
                // Reset all (set every pixel to 100%), Update (just pop —
                // changes were already live-applied during the drag).
                // Matches the editor pattern used elsewhere in the app.
                SafeArea(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 8),
                    child: Row(
                      children: [
                        // Mental-model fix (audit ux-H5): the OLD "Cancel"
                        // button does real work (rewrites every drifted
                        // pixel back to its original) and the OLD "Update"
                        // was a no-op pop (changes already live-applied).
                        // Renamed to match what they do:
                        //   "Discard changes" — actively reverts.
                        //   "Done"            — closes the screen.
                        TextButton.icon(
                          icon: const Icon(Icons.undo, size: 18),
                          label: const Text('Discard changes'),
                          onPressed: _cancel,
                        ),
                        Tooltip(
                          message: state.base.knockout.isEmpty
                              ? 'No knockout pixels to reset'
                              : 'Reset every pixel to 100%',
                          child: TextButton(
                            onPressed: state.base.knockout.isEmpty
                                ? null
                                : () => notifier.clearKnockout(),
                            child: const Text('Reset all'),
                          ),
                        ),
                        const Spacer(),
                        FilledButton.icon(
                          icon: const Icon(Icons.check, size: 18),
                          label: const Text('Done'),
                          onPressed: () => Navigator.of(context).pop(),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
            );
          },
        ),
      ),
    );
  }
}

/// One pixel row: label, horizontal brightness bar, % readout. The bar is
/// non-interactive; the parent's `Listener` handles all gestures.
///
/// Each row is its OWN `ConsumerWidget` and `.select`s on just its index's
/// knockout value (audit perf-H4). Pre-fix, 144 rows all rebuilt + relaid-
/// out at ~33 Hz during a drag because the parent's `data: (state) {...}`
/// rebuilt the entire `Column` on every state change. Now only the rows
/// whose pixel value actually changed rebuild — typically one per drag tick.
class _PixelBar extends ConsumerWidget {
  const _PixelBar({required this.lampId, required this.index});

  final String lampId;
  final int index;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final brightness = ref.watch(controlNotifierProvider(lampId).select(
      (a) => a.value?.base.knockout[index] ?? 100,
    ));
    final fraction = (brightness / 100).clamp(0.0, 1.0);
    final colorScheme = Theme.of(context).colorScheme;
    return SizedBox(
      height: _KnockoutScreenState._rowHeight,
      child: Padding(
        padding: const EdgeInsets.symmetric(
            horizontal: _KnockoutScreenState._hPadding, vertical: 4),
        child: Row(
          children: [
            SizedBox(
              width: _KnockoutScreenState._labelWidth,
              child: Text(
                '#$index',
                style: TextStyle(
                  color: colorScheme.onSurfaceVariant,
                  fontSize: 11,
                  fontFamily: 'monospace',
                ),
              ),
            ),
            Expanded(
              // Match `_KnockoutScreenState._barInsetH`: the gesture math
              // treats the inset as 0/100 grab zones, and the visible bar
              // pulls back from the readout strip by the same amount so
              // a finger reaching 100 % no longer crashes into scroll.
              child: Padding(
                padding: const EdgeInsets.symmetric(
                    horizontal: _KnockoutScreenState._barInsetH),
                child: Container(
                  height: 8,
                  decoration: BoxDecoration(
                    color: colorScheme.onSurfaceVariant.withValues(alpha: 0.35),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  alignment: Alignment.centerLeft,
                  child: FractionallySizedBox(
                    widthFactor: fraction,
                    child: Container(
                      decoration: BoxDecoration(
                        color: colorScheme.primary,
                        borderRadius: BorderRadius.circular(4),
                      ),
                    ),
                  ),
                ),
              ),
            ),
            SizedBox(
              width: _KnockoutScreenState._readoutWidth,
              child: Text(
                '$brightness%',
                textAlign: TextAlign.right,
                style: TextStyle(
                  color: colorScheme.onSurfaceVariant,
                  fontSize: 11,
                  fontFamily: 'monospace',
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
