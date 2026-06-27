import 'package:flutter/material.dart';

import '../../../../core/theme/brand_colors.dart';
import '../../../../core/widgets/app_sheet.dart';
import '../../domain/lamp_color.dart';
import 'lamp_color_swatch.dart';

/// Opens a modal bottom sheet to pick a color via R/G/B (and Warm White when
/// the strip is 4 bpp). Returns the picked [LampColor] or null on cancel.
///
/// If [onLive] is provided it is called on every slider tick with the
/// current color so callers can stream realtime previews to the hardware.
///
/// [bpp]: 3 → strip is RGB only, hide the Warm-White slider and emit W=0.
/// 4 → RGBW strip, expose the W slider.
Future<LampColor?> showColorPickerSheet(
  BuildContext context, {
  required LampColor initial,
  String title = 'Pick a color',
  int bpp = 4,
  ValueChanged<LampColor>? onLive,
}) {
  return showAppSheet<LampColor>(
    context,
    builder: (ctx) => _ColorPickerSheet(
      initial: initial,
      title: title,
      bpp: bpp,
      onLive: onLive,
    ),
  );
}

class _ColorPickerSheet extends StatefulWidget {
  const _ColorPickerSheet({
    required this.initial,
    required this.title,
    required this.bpp,
    this.onLive,
  });
  final LampColor initial;
  final String title;
  final int bpp;
  final ValueChanged<LampColor>? onLive;

  @override
  State<_ColorPickerSheet> createState() => _ColorPickerSheetState();
}

class _ColorPickerSheetState extends State<_ColorPickerSheet> {
  late int _r = widget.initial.r;
  late int _g = widget.initial.g;
  late int _b = widget.initial.b;
  late int _w = widget.bpp == 4 ? widget.initial.w : 0;

  // Round-trips with _r/_g/_b/_w so the user can type a hex code directly
  // (matches the Vue picker — software/lamp-app/src/components/fields/Color.vue).
  late final TextEditingController _hexCtrl =
      TextEditingController(text: _displayHex);
  String? _hexError;

  LampColor get _current => LampColor(
        r: _r,
        g: _g,
        b: _b,
        w: widget.bpp == 4 ? _w : 0,
      );

  String get _displayHex {
    final full = _current.toHex(); // '#RRGGBBWW'
    return widget.bpp == 4 ? full : full.substring(0, 7);
  }

  @override
  void dispose() {
    _hexCtrl.dispose();
    super.dispose();
  }

  void _emitLive() => widget.onLive?.call(_current);

  /// Mirror the current channel values back into the hex field whenever a
  /// slider moves. Skips the write if the field already shows the same hex
  /// so the user's caret position isn't disturbed mid-typing.
  void _syncHexField() {
    if (_hexCtrl.text.toUpperCase() != _displayHex) {
      _hexCtrl.text = _displayHex;
    }
    if (_hexError != null) setState(() => _hexError = null);
  }

  void _setR(int v) {
    setState(() => _r = v);
    _syncHexField();
    _emitLive();
  }

  void _setG(int v) {
    setState(() => _g = v);
    _syncHexField();
    _emitLive();
  }

  void _setB(int v) {
    setState(() => _b = v);
    _syncHexField();
    _emitLive();
  }

  void _setW(int v) {
    setState(() => _w = v);
    _syncHexField();
    _emitLive();
  }

  void _onHexChanged(String input) {
    final parsed = LampColor.tryFromHex(input);
    if (parsed == null) {
      // Show the error inline but don't reject partial typing — the user is
      // probably mid-keystroke; revert validation when they reach a valid
      // length.
      setState(() => _hexError = 'Need #RRGGBB or #RRGGBBWW');
      return;
    }
    setState(() {
      _r = parsed.r;
      _g = parsed.g;
      _b = parsed.b;
      if (widget.bpp == 4) _w = parsed.w;
      _hexError = null;
    });
    _emitLive();
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.only(
        bottom: MediaQuery.of(context).viewInsets.bottom,
      ),
      child: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                widget.title,
                style: const TextStyle(
                  color: BrandColors.lampWhite,
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(height: 16),
              Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  LampColorSwatch(
                    key: const Key('preview-swatch'),
                    color: LampColor(
                      r: _r,
                      g: _g,
                      b: _b,
                      w: widget.bpp == 4 ? _w : 0,
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: TextField(
                      controller: _hexCtrl,
                      onChanged: _onHexChanged,
                      textCapitalization: TextCapitalization.characters,
                      style: const TextStyle(
                        color: BrandColors.lampWhite,
                        fontSize: 14,
                        fontFamily: 'monospace',
                      ),
                      decoration: InputDecoration(
                        isDense: true,
                        labelText: 'Hex',
                        labelStyle: const TextStyle(
                          color: BrandColors.fogGrey,
                          fontSize: 12,
                        ),
                        errorText: _hexError,
                        border: const OutlineInputBorder(),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 16),
              _ChannelSlider(
                label: 'Red',
                value: _r,
                trackColor: Colors.redAccent,
                onChanged: _setR,
              ),
              _ChannelSlider(
                label: 'Green',
                value: _g,
                trackColor: Colors.greenAccent,
                onChanged: _setG,
              ),
              _ChannelSlider(
                label: 'Blue',
                value: _b,
                trackColor: Colors.blueAccent,
                onChanged: _setB,
              ),
              if (widget.bpp == 4)
                _ChannelSlider(
                  key: const Key('ww-slider'),
                  label: 'Warm White',
                  value: _w,
                  trackColor: BrandColors.warmWhite,
                  onChanged: _setW,
                ),
              const SizedBox(height: 12),
              Row(
                children: [
                  TextButton(
                    onPressed: () => Navigator.pop(context),
                    child: const Text('Cancel'),
                  ),
                  const Spacer(),
                  FilledButton(
                    onPressed: () => Navigator.pop(context, _current),
                    child: const Text('Done'),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ChannelSlider extends StatelessWidget {
  const _ChannelSlider({
    super.key,
    required this.label,
    required this.value,
    required this.trackColor,
    required this.onChanged,
  });

  final String label;
  final int value;
  final Color trackColor;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          SizedBox(
            width: 72,
            child: Text(
              label,
              style: const TextStyle(
                color: BrandColors.lampWhite,
                fontSize: 13,
              ),
            ),
          ),
          Expanded(
            child: SliderTheme(
              data: SliderTheme.of(context).copyWith(
                activeTrackColor: trackColor,
                thumbColor: BrandColors.lampWhite,
                inactiveTrackColor: BrandColors.slateGrey.withValues(alpha: 0.4),
              ),
              child: Slider(
                min: 0,
                max: 255,
                divisions: 255,
                value: value.toDouble(),
                onChanged: (v) => onChanged(v.round()),
              ),
            ),
          ),
          SizedBox(
            width: 36,
            child: Text(
              '$value',
              textAlign: TextAlign.right,
              style: const TextStyle(
                color: BrandColors.fogGrey,
                fontSize: 12,
                fontFamily: 'monospace',
              ),
            ),
          ),
        ],
      ),
    );
  }
}

