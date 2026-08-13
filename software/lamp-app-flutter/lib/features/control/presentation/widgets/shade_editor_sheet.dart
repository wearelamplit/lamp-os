import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/widgets/app_sheet.dart';
import '../../application/control_notifier.dart';
import '../../application/control_state.dart';
import '../../domain/lamp_color.dart';
import 'color_stops_sheet.dart';

/// Hard cap on shade gradient stops. Firmware interp handles arbitrary N; the
/// cap is UX-only so the modal stays scannable.
const int _kMaxShadeStops = 6;

/// Which color list a shared color editor drives.
class ColorChannelSpec {
  const ColorChannelSpec({
    required this.title,
    required this.selectColors,
    required this.setLive,
  });

  final String title;
  final List<LampColor> Function(ControlState) selectColors;
  final void Function(ControlNotifier, List<LampColor>) setLive;
}

/// Spec for shade segment [segIdx]. Every segment live-previews over
/// CHAR_SHADE_COLORS and persists through CHAR_COMMIT on Save.
ColorChannelSpec shadeSegmentSpec(int segIdx, String title) => ColorChannelSpec(
      title: title,
      selectColors: (s) => segIdx < s.shade.segments.length
          ? s.shade.segments[segIdx].colors
          : s.shade.colors,
      setLive: (n, c) => n.setShadeSegmentColors(segIdx, c),
    );

/// Modal sheet for editing the shade gradient stops. A thin [ColorStopsSheet]
/// caller: live-previews via [ColorChannelSpec.setLive] as the user edits and
/// commits through CHAR_COMMIT on Save. The wisp edit-session spans the whole
/// sheet (opened by ShadeCard around [showShadeEditorSheet]).
class ShadeEditorSheet extends ConsumerWidget {
  const ShadeEditorSheet({
    super.key,
    required this.lampId,
    required this.spec,
  });

  final String lampId;
  final ColorChannelSpec spec;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final state = ref.watch(
      controlNotifierProvider(lampId).select((async) => async.value),
    );
    if (state == null) return const SizedBox.shrink();

    final notifier = ref.read(controlNotifierProvider(lampId).notifier);
    return ColorStopsSheet(
      initial: spec.selectColors(state),
      title: spec.title,
      description: 'These colors blend into a gradient along the strip.',
      max: _kMaxShadeStops,
      bpp: state.shade.bpp,
      onChanged: (colors) => spec.setLive(notifier, colors),
      onSave: (colors) async {
        spec.setLive(notifier, colors);
        await notifier.commit();
      },
    );
  }
}

Future<void> showShadeEditorSheet(
  BuildContext context, {
  required String lampId,
  required ColorChannelSpec spec,
}) {
  return showAppSheet<void>(
    context,
    builder: (ctx) => FractionallySizedBox(
      heightFactor: 0.6,
      child: ShadeEditorSheet(lampId: lampId, spec: spec),
    ),
  );
}
