import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../../core/routing/routes.dart';
import '../../../core/theme/app_spacing.dart';
import '../../../core/widgets/app_sheet.dart';
import '../../../core/widgets/app_snackbar.dart';
import '../../../core/widgets/back_button_leading.dart';
import '../../../core/widgets/form_section.dart';
import '../../../core/widgets/friendly_error.dart';
import '../../../core/widgets/info_panel.dart';
import '../../../core/widgets/nav_row.dart';
import '../../../core/widgets/number_input_dialog.dart';
import '../../control/application/advanced_session.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../../control/domain/sections.dart';
import '../../control/application/settings_blob_too_large_exception.dart';
import '../../firmware/presentation/firmware_push_lock.dart';

/// Advanced LED settings: pixel count per segment, data pin per segment, and
/// strip-type (LED byte order). Data pin and strip type show only in advanced
/// mode, and data pin only for a lamp whose sections report a pin. Unlocked
/// from the Info tab via the 5-tap secret on the lamplit wordmark, or directly
/// toggleable from the Setup screen once advanced is on.
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
// 20 Ah battery. Interpolate the two firmware draw anchors (LED-only) to the
// ceiling, plus the MCU+radio floor the anchors omit: a dark lamp measures
// ~134 mA total vs the ~55 mA LED-idle anchor, so ~80 mA is the ESP32 + radio.
// ESP32 + BLE/mesh average draw. Idle alone is lower (~80), an active app
// session or config AP is higher; this leans toward the active case.
const _mcuRadioBaselineMa = 120;
// INIU 20000 mAh bank: ~75 Wh in the cells (20 Ah at ~3.7 V), not 20 Ah at 5 V.
// Deliberately conservative until measured: 0.78 covers the 5 V boost loss plus
// real delivered capacity running under nominal, so ~11700 mAh usable at 5 V.
const _bankWh = 75.0;
const _deliveredFraction = 0.78;
const _railVolts = 5.0;
String batteryEstimateLabel(int idleMa, int fullMa, int ceiling) {
  if (fullMa <= idleMa) return '';
  final drawMa =
      idleMa + (fullMa - idleMa) * ceiling / 255.0 + _mcuRadioBaselineMa;
  if (drawMa <= 0) return '';
  const usableMahAt5v = _bankWh * _deliveredFraction / _railVolts * 1000.0;
  final hours = usableMahAt5v / drawMa;
  return '~${hours.toStringAsFixed(hours < 10 ? 1 : 0)} h on a 20000 mAh bank (estimated)';
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
  List<int?>? _origShadePins;
  List<int?>? _origBasePins;

  /// True from an Update tap until its write and the reboot reload after it
  /// end.
  bool _saving = false;

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
    for (var i = 0; i < (_origShadePins?.length ?? 0); i++) {
      final pin = _origShadePins![i];
      if (pin != null) n.setSegmentPin('shade', i, pin);
    }
    for (var i = 0; i < (_origBasePins?.length ?? 0); i++) {
      final pin = _origBasePins![i];
      if (pin != null) n.setSegmentPin('base', i, pin);
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
      body: FirmwarePushLock(
        lampId: widget.lampId,
        child: async.when(
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
            _origShadePins ??= state.shade.segments.map((s) => s.pin).toList();
            _origBasePins ??= state.base.segments.map((s) => s.pin).toList();

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
                      InfoPanel(
                        child: Text(
                          'Match these to the physical strips you built '
                          'into the lamp. Mis-matched LED count or '
                          'byte-order will show as wrong-coloured or '
                          'partially-lit segments.'
                          '${showByteOrder && _showsDataPin(state) ? ' A '
                              'wrong data pin leaves that strip dark.' : ''}',
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
                            ..._dataPinRows(state, 'shade', notifier),
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
                            ..._dataPinRows(state, 'base', notifier),
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
                          icon: _saving
                              ? const SizedBox.square(
                                  dimension: 18, // deliberate dimension, not spacing
                                  child: CircularProgressIndicator(strokeWidth: 2),
                                )
                              : const Icon(Icons.check, size: 18),
                          label: const Text('Update'),
                          onPressed: _saving ? null : () async {
                            setState(() => _saving = true);
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
                                AppSnackbar.error(
                                  context,
                                  advancedLedsSaveMessage(
                                    mismatches,
                                    shadeStrips: state.shade.segments.length,
                                    baseStrips: state.base.segments.length,
                                  ),
                                );
                              }
                            } on SettingsBlobTooLargeException catch (e) {
                              if (!context.mounted) return;
                              AppSnackbar.error(context, e.message);
                            } catch (e) {
                              if (!context.mounted) return;
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(content: Text('Save failed: $e')),
                              );
                            } finally {
                              if (mounted) setState(() => _saving = false);
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
      ),
    );
  }
  static bool _hasDataPin(Segment s) =>
      s.pin != null && (s.builtinPin ?? 0) != 0;

  static bool _showsDataPin(ControlState state) =>
      [...state.shade.segments, ...state.base.segments].any(_hasDataPin);

  /// A Data pin row per segment the lamp reports a pin and a strip for.
  List<Widget> _dataPinRows(
      ControlState state, String role, ControlNotifier notifier) {
    final segs = role == 'shade' ? state.shade.segments : state.base.segments;
    final roleLabel = role == 'shade' ? 'Shade' : 'Base';
    return [
      for (var i = 0; i < segs.length; i++)
        if (_hasDataPin(segs[i]))
          _DataPinRow(
            label: segs.length > 1
                ? '${segs[i].name} data pin'
                : '$roleLabel data pin',
            pin: segs[i].pin!,
            builtinPin: segs[i].builtinPin!,
            usedBy: _pinOwners(state, exceptRole: role, exceptIndex: i),
            onChanged: (p) => notifier.setSegmentPin(role, i, p),
          ),
    ];
  }

  /// Every other strip's pin in use, named the way its rows are.
  static Map<int, String> _pinOwners(ControlState state,
      {required String exceptRole, required int exceptIndex}) {
    final owners = <int, String>{};
    for (final (role, label, segs) in [
      ('shade', 'Shade strip', state.shade.segments),
      ('base', 'Base strip', state.base.segments),
    ]) {
      for (var i = 0; i < segs.length; i++) {
        if (role == exceptRole && i == exceptIndex) continue;
        final pin = segs[i].effectivePin;
        if (pin != null) owners[pin] = segs.length > 1 ? segs[i].name : label;
      }
    }
    return owners;
  }
}

/// The friendly message for the fields an Advanced LEDs save reported as not
/// taken ([ControlNotifier.applyAdvancedLedsAndReboot]). The strip counts name
/// a pin the way its rows are named.
@visibleForTesting
String advancedLedsSaveMessage(List<String> mismatches,
    {int shadeStrips = 1, int baseStrips = 1}) {
  final lines = <String>{};
  for (final m in mismatches) {
    final dot = m.indexOf('.');
    final role = dot > 0 ? m.substring(0, dot) : '';
    final field = dot > 0 ? m.substring(dot + 1) : '';
    final label = switch (role) {
      'shade' => 'Shade',
      'base' => 'Base',
      _ => null,
    };
    lines.add(switch ((label, field)) {
      (final l?, 'pin') =>
        "${_pinName(l, role == 'shade' ? shadeStrips : baseStrips)} didn't "
            'take. It clashes with another strip, a button or a touch pad, so '
            'the lamp kept its built-in pin.',
      (final l?, 'byteOrder' || 'bpp') =>
        "$l strip type didn't take, so the lamp kept its old one.",
      (final l?, 'px') =>
        "$l LED count didn't take, so the lamp kept its old count.",
      _ => "Your lamp didn't come back after restarting. Bring your phone "
          'closer and try again.',
    });
  }
  return lines.join(' ');
}

String _pinName(String role, int strips) =>
    strips > 1 ? 'A ${role.toLowerCase()} strip data pin' : '$role data pin';

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
          SizedBox(
            width: double.infinity,
            child: SegmentedButton<String>(
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
          SizedBox(
            width: double.infinity,
            child: SegmentedButton<int>(
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

// GPIOs the firmware accepts for a strip's data line (isStripDataPin).
const _stripDataPins = [
  2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33,
];

class _DataPinRow extends StatelessWidget {
  const _DataPinRow({
    required this.label,
    required this.pin,
    required this.builtinPin,
    required this.usedBy,
    required this.onChanged,
  });
  final String label;

  /// The saved pin, 0 for the built-in one.
  final int pin;
  final int builtinPin;

  /// Pins other strips run on, with the strip's name.
  final Map<int, String> usedBy;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    final current = pin == builtinPin ? 0 : pin;
    String name(int p) => p == 0 ? 'Built-in (GPIO $builtinPin)' : 'GPIO $p';
    return NavRow(
      icon: Icons.settings_input_component,
      title: label,
      subtitle: name(current),
      onTap: () => _pick(context, current, name),
    );
  }

  Future<void> _pick(
      BuildContext context, int current, String Function(int) name) async {
    var changed = false;
    final picked = await showAppSheet<int>(
      context,
      builder: (ctx) {
        final colorScheme = Theme.of(ctx).colorScheme;
        Widget option(int p) {
          final owner = usedBy[p == 0 ? builtinPin : p];
          return ListTile(
            title: Text(name(p)),
            subtitle: owner == null ? null : Text('Used by $owner'),
            enabled: owner == null,
            selected: p == current,
            trailing: p == current
                ? Icon(Icons.check, color: colorScheme.primary)
                : null,
            onTap: () {
              changed = p != current;
              Navigator.pop(ctx, p);
            },
          );
        }

        return SafeArea(
          child: ConstrainedBox(
            constraints: BoxConstraints(
              maxHeight: MediaQuery.of(ctx).size.height * 0.7,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(
                      AppSpace.lg, AppSpace.md, AppSpace.lg, AppSpace.sm),
                  child: Text(label,
                      style: Theme.of(ctx).textTheme.titleMedium),
                ),
                Flexible(
                  child: ListView(
                    shrinkWrap: true,
                    children: [
                      option(0),
                      for (final p in _stripDataPins)
                        if (p != builtinPin) option(p),
                    ],
                  ),
                ),
                const SizedBox(height: AppSpace.sm),
              ],
            ),
          ),
        );
      },
    );
    if (changed && picked != null) onChanged(picked);
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
