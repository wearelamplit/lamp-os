import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/widgets/lamp_card.dart';
import '../../application/control_notifier.dart';

class BrightnessCard extends ConsumerWidget {
  const BrightnessCard({
    super.key,
    required this.lampId,
    this.onEditSessionChanged,
  });

  final String lampId;

  /// Fires `true` when the slider drag starts and `false` when it ends.
  /// Wired into `ControlNotifier.setEditSession` so the lamp drops
  /// wisp-sourced brightness overrides for the duration of the drag.
  final ValueChanged<bool>? onEditSessionChanged;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final value = ref.watch(
      controlNotifierProvider(lampId)
          .select((async) => async.value?.lamp.brightness ?? 0),
    );
    final notifier = ref.read(controlNotifierProvider(lampId).notifier);
    return LampCard(
      child: Row(
        children: [
          const Icon(Icons.brightness_low),
          Expanded(
            child: Slider(
              min: 0,
              max: 100,
              divisions: 100,
              value: value.toDouble(),
              onChanged: (v) => notifier.setBrightness(v.toInt()),
              onChangeStart: (_) => onEditSessionChanged?.call(true),
              onChangeEnd: (v) {
                notifier.setBrightness(v.toInt());
                notifier.scheduleBrightnessCommit();
                onEditSessionChanged?.call(false);
              },
            ),
          ),
          const Icon(Icons.brightness_high),
        ],
      ),
    );
  }
}
