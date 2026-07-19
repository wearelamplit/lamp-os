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
import '../../../core/widgets/number_input_dialog.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';

/// Advanced LED settings: pixel count per segment + strip-type (LED byte
/// order). Unlocked from the Info tab via the 5-tap secret on the lamplit
/// wordmark, or directly toggleable from the Setup screen once advanced is on.
///
/// Per-segment px is editable only on single-segment roles (standard); on
/// multi-segment roles (snafu) the firmware owns the geometry via StripSpec.
///
/// Strip-type options map to NeoPixel byte-order flags. The `byteOrder`
/// field on each section is the source of truth; `bpp` is kept in sync
/// (4 for GRBW, 3 for GRB/BGR).
///
/// The bottom Cancel/Update action row: Update calls
/// `applyAdvancedLedsAndReboot` → `writeSettingsBlob(reboot:true)` → firmware
/// reboots and reconnects (~8–12 s), then pops on success. Cancel rewinds
/// per-segment px and byte order to the pre-screen-open values without
/// writing to the lamp.
// 20 Ah battery. Interpolate the two firmware draw anchors to the ceiling.
String batteryEstimateLabel(int idleMa, int fullMa, int ceiling) {
  if (fullMa <= idleMa) return '';
  final drawMa = idleMa + (fullMa - idleMa) * ceiling / 255.0;
  if (drawMa <= 0) return '';
  final hours = 20000 / drawMa;
  return '~${hours.toStringAsFixed(hours < 10 ? 1 : 0)} h on a 20 Ah battery (estimated)';
}

class AdvancedLedsScreen extends ConsumerStatefulWidget {
  const AdvancedLedsScreen({super.key, required this.lampId});
  final String lampId;

  @override
  ConsumerState<AdvancedLedsScreen> createState() =>
      _AdvancedLedsScreenState();
}

class _AdvancedLedsScreenState extends ConsumerState<AdvancedLedsScreen> {
  // Per-segment px snapshots taken on the first non-loading paint.
  // Single-element for standard/old-firmware; multi-element for snafu.
  // Cancel restores from here.
  List<int>? _origShadePxs;
  String? _origShadeByteOrder;
  List<int>? _origBasePxs;
  String? _origBaseByteOrder;

  void _cancel() {
    final n = ref.read(controlNotifierProvider(widget.lampId).notifier);
    for (var i = 0; i < (_origShadePxs?.length ?? 0); i++) {
      n.setSegmentPx('shade', i, _origShadePxs![i]);
    }
    if (_origShadeByteOrder != null) n.setShadeByteOrder(_origShadeByteOrder!);
    for (var i = 0; i < (_origBasePxs?.length ?? 0); i++) {
      n.setSegmentPx('base', i, _origBasePxs![i]);
    }
    if (_origBaseByteOrder != null) n.setBaseByteOrder(_origBaseByteOrder!);
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
          // Capture on first paint, never overwrite. The user's mid-
          // edit values come through `state` later; Cancel needs to
          // revert to where things were when the screen opened.
          _origShadeByteOrder ??= state.shade.byteOrder;
          _origBaseByteOrder ??= state.base.byteOrder;
          _origShadePxs ??= state.shade.segments.isEmpty
              ? [state.shade.px]
              : state.shade.segments.map((s) => s.px).toList();
          _origBasePxs ??= state.base.segments.isEmpty
              ? [state.base.px]
              : state.base.segments.map((s) => s.px).toList();

          final notifier =
              ref.read(controlNotifierProvider(widget.lampId).notifier);
          final showByteOrder =
              ref.watch(effectiveAdvancedProvider(widget.lampId));
          // Only the standard lamp has user-configurable LED counts; custom
          // variants are fixed geometry, so their counts are display-only.
          // Null lampType (older firmware) reads as standard.
          final ledCountsEditable =
              (state.lamp.lampType ?? 'standard') == 'standard';
          return Column(
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
                    FormSection(
                      title: 'Power',
                      children: [
                        _BatterySaverPicker(
                          ceiling: state.lamp.brightnessCeiling,
                          idleMa: state.base.drawIdleMa,
                          fullMa: state.base.drawFullMa,
                          onChanged: notifier.setLampBrightnessCeiling,
                        ),
                      ],
                    ),
                    const SizedBox(height: AppSpace.lg),
                    // Shade strip first: it sits physically above the base.
                    FormSection(
                      title: 'Shade strip',
                      children: [
                        if (state.shade.segments.isNotEmpty)
                          for (var i = 0; i < state.shade.segments.length; i++)
                            _LedCountRow(
                              label: '${state.shade.segments[i].name} LED count',
                              value: state.shade.segments[i].px,
                              readOnly:
                                  !ledCountsEditable || state.shade.segments.length > 1,
                              onChanged: (v) => notifier.setSegmentPx('shade', i, v),
                            )
                        else
                          _LedCountRow(
                            label: 'Shade LED count',
                            value: state.shade.px,
                            readOnly: !ledCountsEditable,
                            onChanged: (v) => notifier.setSegmentPx('shade', 0, v),
                          ),
                        if (showByteOrder)
                          _StripTypePicker(
                            selected: state.shade.byteOrder,
                            onChanged: notifier.setShadeByteOrder,
                          ),
                      ],
                    ),
                    const SizedBox(height: AppSpace.lg),
                    // Knockout masks base pixels only, so it nests under the
                    // base strip rather than living as its own Setup tile.
                    FormSection(
                      title: 'Base strip',
                      children: [
                        if (state.base.segments.isNotEmpty)
                          for (var i = 0; i < state.base.segments.length; i++)
                            _LedCountRow(
                              label: '${state.base.segments[i].name} LED count',
                              value: state.base.segments[i].px,
                              readOnly:
                                  !ledCountsEditable || state.base.segments.length > 1,
                              onChanged: (v) => notifier.setSegmentPx('base', i, v),
                            )
                        else
                          _LedCountRow(
                            label: 'Base LED count',
                            value: state.base.px,
                            readOnly: !ledCountsEditable,
                            onChanged: (v) => notifier.setSegmentPx('base', 0, v),
                          ),
                        if (showByteOrder)
                          _StripTypePicker(
                            selected: state.base.byteOrder,
                            onChanged: notifier.setBaseByteOrder,
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
          );
        },
      ),
    );
  }
}

class _StripTypePicker extends StatelessWidget {
  const _StripTypePicker({required this.selected, required this.onChanged});
  final String selected;
  final ValueChanged<String> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(AppSpace.lg),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('LED type', style: Theme.of(context).textTheme.bodySmall),
          const SizedBox(height: AppSpace.sm),
          SegmentedButton<String>(
            showSelectedIcon: false,
            style: SegmentedButton.styleFrom(
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(AppRadius.card),
              ),
            ),
            segments: const [
              ButtonSegment(value: 'GRBW', label: Text('GRBW')),
              ButtonSegment(value: 'GRB', label: Text('GRB')),
              ButtonSegment(value: 'BGR', label: Text('BGR')),
            ],
            selected: {selected},
            onSelectionChanged: (s) => onChanged(s.first),
          ),
        ],
      ),
    );
  }
}

class _BatterySaverPicker extends StatelessWidget {
  const _BatterySaverPicker({
    required this.ceiling,
    required this.idleMa,
    required this.fullMa,
    required this.onChanged,
  });
  final int ceiling;
  final int idleMa;
  final int fullMa;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    final estimate = batteryEstimateLabel(idleMa, fullMa, ceiling);
    return Padding(
      padding: const EdgeInsets.all(AppSpace.lg),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Battery Saver', style: Theme.of(context).textTheme.bodySmall),
          const SizedBox(height: AppSpace.sm),
          SegmentedButton<int>(
            showSelectedIcon: false,
            style: SegmentedButton.styleFrom(
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(AppRadius.card),
              ),
            ),
            segments: const [
              ButtonSegment(value: 120, label: Text('Saver')),
              ButtonSegment(value: 170, label: Text('Standard')),
              ButtonSegment(value: 230, label: Text('Bright')),
            ],
            selected: {ceiling},
            onSelectionChanged: (s) => onChanged(s.first),
          ),
          if (estimate.isNotEmpty) ...[
            const SizedBox(height: AppSpace.sm),
            Text(estimate, style: Theme.of(context).textTheme.bodySmall),
          ],
        ],
      ),
    );
  }
}

class _LedCountRow extends StatelessWidget {
  const _LedCountRow({
    required this.label,
    required this.value,
    required this.onChanged,
    this.readOnly = false,
  });
  final String label;
  final int value;
  final bool readOnly;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return NavRow(
      icon: Icons.lightbulb_outline,
      title: label,
      subtitle: '$value LEDs',
      onTap: readOnly
          ? null
          : () => showNumberInputDialog(
                context,
                title: label,
                label: 'LED count',
                initial: value,
                min: 1,
                max: 255,
                onSave: onChanged,
              ),
    );
  }
}
