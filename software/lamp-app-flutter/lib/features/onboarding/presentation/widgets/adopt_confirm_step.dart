import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ble/ble_client_provider.dart';
import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/friendly_error.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/recolored_critter.dart';
import '../../application/add_lamp_notifier.dart';
import '../../application/adopt_pulse_controller.dart';

class AdoptConfirmStep extends ConsumerStatefulWidget {
  const AdoptConfirmStep({super.key});

  @override
  ConsumerState<AdoptConfirmStep> createState() => _AdoptConfirmStepState();
}

class _AdoptConfirmStepState extends ConsumerState<AdoptConfirmStep>
    with WidgetsBindingObserver {
  late final AdoptPulseController _ctrl;
  // Colours the scan step captured at tap-time (AddLampState). We connect for
  // the pulse right after this, which stops the lamp advertising — it then
  // ages out of the nearby-scan list, so re-reading colours from there would
  // collapse to black. The wizard snapshot survives that.
  late final LampColor _shadeColor;
  late final LampColor _baseColor;
  Object? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _ctrl = AdoptPulseController(ref.read(bleClientProvider));
    final add = ref.read(addLampNotifierProvider);
    _shadeColor = _rgbToColor(add.shadeRgb);
    _baseColor = _rgbToColor(add.baseRgb);
    _startPulse();
  }

  static LampColor _rgbToColor(int rgb) =>
      LampColor(r: (rgb >> 16) & 0xFF, g: (rgb >> 8) & 0xFF, b: rgb & 0xFF, w: 0);

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    // ponytail: fire-and-forget; synchronous part (cancel timer, set _stopped)
    // runs immediately, async write/disconnect drains on next event turn.
    unawaited(_ctrl.stop());
    super.dispose();
  }

  // Android freezes the process on screen-off, pausing the pulse timer and
  // stranding the lamp mid-pulse with the edit-session held. Release it
  // cleanly when we background and re-arm the pulse when we come back.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused) {
      unawaited(_ctrl.stop());
    } else if (state == AppLifecycleState.resumed && mounted) {
      _startPulse();
    }
  }

  void _startPulse() async {
    if (!mounted) return;
    final deviceId = ref.read(addLampNotifierProvider).deviceId;
    try {
      await _ctrl.start(deviceId, _shadeColor);
      if (mounted && _error != null) setState(() => _error = null);
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = e);
    }
  }

  Future<void> _adopt() async {
    await _ctrl.stop();
    if (!mounted) return;
    ref.read(addLampNotifierProvider.notifier).next();
  }

  Future<void> _cancel() async {
    await _ctrl.stop();
    if (!mounted) return;
    ref.read(addLampNotifierProvider.notifier).previous();
  }

  @override
  Widget build(BuildContext context) {
    final deviceId =
        ref.watch(addLampNotifierProvider.select((s) => s.deviceId));
    final textTheme = Theme.of(context).textTheme;

    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) async {
        if (!didPop) await _cancel();
      },
      child: Padding(
        padding: const EdgeInsets.all(AppSpace.xl),
        child: SizedBox(
          width: double.infinity,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              RecoloredCritter(
                deviceId: deviceId,
                shadeColors: [_shadeColor],
                baseColors: [_baseColor],
                size: 120,
              ),
              const SizedBox(height: AppSpace.md),
              Text(
                'Found your stray?',
                textAlign: TextAlign.center,
                style: textTheme.headlineSmall,
              ),
              const SizedBox(height: AppSpace.sm),
              Text(
                'The one glowing at you is the stray you tapped. Take it in?',
                textAlign: TextAlign.center,
                style: textTheme.bodyMedium,
              ),
              if (_error != null) ...[
                const SizedBox(height: AppSpace.md),
                FriendlyError.inline(
                  title: "Couldn't reach it — move closer",
                  rawError: _error,
                ),
                const SizedBox(height: AppSpace.sm),
                OutlinedButton(
                  onPressed: () {
                    setState(() => _error = null);
                    _startPulse();
                  },
                  child: const Text('Retry'),
                ),
              ],
              const Spacer(),
              Row(
                children: [
                  TextButton(
                    onPressed: _cancel,
                    child: const Text('Cancel'),
                  ),
                  const Spacer(),
                  FilledButton(
                    onPressed: _adopt,
                    child: const Text('Adopt'),
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
