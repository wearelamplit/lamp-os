import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/form_section.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/info_panel.dart';
import '../../../core/widgets/nav_row.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/presentation/widgets/connecting_view.dart';
import '../../control/presentation/widgets/disconnect_aware_body.dart';

/// Advanced LED settings — number of pixels per strip + strip-type (LED
/// byte order). Unlocked from the Info tab via the 5-tap secret on the
/// lamplit wordmark, or directly toggleable from the Setup screen once
/// advanced is on.
///
/// Strip-type options map directly to NeoPixel byte-order flags. The
/// `byteOrder` field on each section is the source of truth; `bpp` is
/// kept in sync (4 for GRBW, 3 for GRB/BGR).
///   - **GRBW** — 4 bpp NeoPixel (`NEO_GRBW`). True warm-white channel.
///   - **GRB**  — 3 bpp NeoPixel (`NEO_GRB`). Standard RGB ordering.
///   - **BGR**  — 3 bpp NeoPixel (`NEO_BGR`). For strips whose hardware
///     ships red and blue swapped from the GRB norm.
///
/// The bottom Cancel/Update action row: Update calls
/// `applyAdvancedLedsAndReboot` → `writeSettingsBlob(reboot:true)` → firmware
/// reboots and reconnects (~8–12 s), then pops on success. Cancel rewinds
/// the four fields (base.px, base.byteOrder, shade.px, shade.byteOrder) to
/// their pre-screen-open values without writing to the lamp.
class AdvancedLedsScreen extends ConsumerStatefulWidget {
  const AdvancedLedsScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<AdvancedLedsScreen> createState() =>
      _AdvancedLedsScreenState();
}

class _AdvancedLedsScreenState extends ConsumerState<AdvancedLedsScreen> {
  // Snapshot of the four fields this screen edits, taken on the first
  // non-loading paint. Cancel restores from here.
  int? _origBasePx;
  String? _origBaseByteOrder;
  int? _origShadePx;
  String? _origShadeByteOrder;

  void _cancel() {
    final n = ref.read(controlNotifierProvider(widget.lampId).notifier);
    if (_origBasePx != null) n.setBasePx(_origBasePx!);
    if (_origBaseByteOrder != null) n.setBaseByteOrder(_origBaseByteOrder!);
    if (_origShadePx != null) n.setShadePx(_origShadePx!);
    if (_origShadeByteOrder != null) {
      n.setShadeByteOrder(_origShadeByteOrder!);
    }
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final async = ref.watch(controlNotifierProvider(widget.lampId));
    return Scaffold(
      appBar: AppBar(
        leading: const BackButtonLeading(),
        title: const Text('LED setup'),
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
          // Capture on first paint, never overwrite — the user's mid-
          // edit values come through `state` later and we want Cancel
          // to revert to where things were when the screen opened.
          _origBasePx ??= state.base.px;
          _origBaseByteOrder ??= state.base.byteOrder;
          _origShadePx ??= state.shade.px;
          _origShadeByteOrder ??= state.shade.byteOrder;

          final notifier =
              ref.read(controlNotifierProvider(widget.lampId).notifier);
          final showByteOrder =
              ref.watch(effectiveAdvancedProvider(widget.lampId));
          return DisconnectAwareBody(
            lampId: widget.lampId,
            child: Column(
            children: [
              Expanded(
                child: ListView(
                  padding: const EdgeInsets.all(AppSpace.lg),
                  children: [
                    const InfoPanel(
                      child: Text(
                        'Match these to the physical strips you built '
                        'into the lamp. Mis-matched LED count or '
                        'byte-order will show as wrong-coloured or '
                        'partially-lit segments.',
                      ),
                    ),
                    const SizedBox(height: AppSpace.lg),
                    // Shade strip first — it sits physically above the base.
                    FormSection(
                      title: 'Shade strip',
                      children: [
                        Padding(
                          padding: const EdgeInsets.all(AppSpace.lg),
                          child: _PixelCountField(
                            initial: state.shade.px,
                            label: 'Shade LED count',
                            onChanged: notifier.setShadePx,
                          ),
                        ),
                        if (showByteOrder)
                          Padding(
                            padding: const EdgeInsets.all(AppSpace.lg),
                            child: SegmentedButton<String>(
                              showSelectedIcon: false,
                              segments: const [
                                ButtonSegment(value: 'GRBW', label: Text('GRBW')),
                                ButtonSegment(value: 'GRB', label: Text('GRB')),
                                ButtonSegment(value: 'BGR', label: Text('BGR')),
                              ],
                              selected: {state.shade.byteOrder},
                              onSelectionChanged: (s) =>
                                  notifier.setShadeByteOrder(s.first),
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: AppSpace.lg),
                    // Knockout masks base pixels only, so it nests under the
                    // base strip rather than living as its own Setup tile.
                    FormSection(
                      title: 'Base strip',
                      children: [
                        Padding(
                          padding: const EdgeInsets.all(AppSpace.lg),
                          child: _PixelCountField(
                            initial: state.base.px,
                            label: 'Base LED count',
                            onChanged: notifier.setBasePx,
                          ),
                        ),
                        if (showByteOrder)
                          Padding(
                            padding: const EdgeInsets.all(AppSpace.lg),
                            child: SegmentedButton<String>(
                              showSelectedIcon: false,
                              segments: const [
                                ButtonSegment(value: 'GRBW', label: Text('GRBW')),
                                ButtonSegment(value: 'GRB', label: Text('GRB')),
                                ButtonSegment(value: 'BGR', label: Text('BGR')),
                              ],
                              selected: {state.base.byteOrder},
                              onSelectionChanged: (s) =>
                                  notifier.setBaseByteOrder(s.first),
                            ),
                          ),
                        NavRow(
                          icon: Icons.grid_on,
                          title: 'Per-pixel knockout',
                          subtitle: '${state.base.knockout.length} pixel(s) masked',
                          onTap: () =>
                              context.push(AppRoutes.knockout(widget.lampId)),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              SafeArea(
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                      horizontal: AppSpace.lg, vertical: AppSpace.sm),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        'Saving restarts the lamp (~10s).',
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                      Row(
                    children: [
                      TextButton.icon(
                        icon: const Icon(Icons.close, size: 18),
                        label: const Text('Cancel'),
                        onPressed: _cancel,
                      ),
                      const Spacer(),
                      FilledButton.icon(
                        icon: const Icon(Icons.check, size: 18),
                        label: const Text('Update'),
                        onPressed: () async {
                          try {
                            final mismatches =
                                await notifier.applyAdvancedLedsAndReboot(
                              base: state.base,
                              shade: state.shade,
                            );
                            if (!context.mounted) return;
                            if (mismatches.isEmpty) {
                              Navigator.of(context).pop();
                            } else {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content: Text(
                                    "Save didn't take: ${mismatches.join(', ')}",
                                  ),
                                  duration: const Duration(seconds: 6),
                                ),
                              );
                            }
                          } catch (e) {
                            if (!context.mounted) return;
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(content: Text('Save failed: $e')),
                            );
                          }
                        },
                      ),
                    ],
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
    );
  }
}

class _PixelCountField extends StatefulWidget {
  const _PixelCountField({
    required this.initial,
    required this.label,
    required this.onChanged,
  });
  final int initial;
  final String label;
  final ValueChanged<int> onChanged;

  @override
  State<_PixelCountField> createState() => _PixelCountFieldState();
}

class _PixelCountFieldState extends State<_PixelCountField> {
  late final _ctrl = TextEditingController(text: '${widget.initial}');

  @override
  void didUpdateWidget(_PixelCountField old) {
    super.didUpdateWidget(old);
    if (old.initial != widget.initial && !_ctrl.value.composing.isValid) {
      _ctrl.text = '${widget.initial}';
    }
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return TextField(
      controller: _ctrl,
      keyboardType: TextInputType.number,
      decoration: InputDecoration(labelText: widget.label),
      onChanged: (v) {
        final n = int.tryParse(v);
        if (n != null && n > 0 && n <= 255) widget.onChanged(n);
      },
    );
  }
}
