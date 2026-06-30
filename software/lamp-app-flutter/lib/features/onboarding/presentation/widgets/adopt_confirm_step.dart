import 'dart:async';

import 'package:collection/collection.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_svg/flutter_svg.dart';

import '../../../../core/ble/ble_client_provider.dart';
import '../../../../core/theme/app_spacing.dart';
import '../../../../core/widgets/friendly_error.dart';
import '../../../control/domain/lamp_color.dart';
import '../../../control/presentation/widgets/critter_asset.dart';
import '../../../nearby/application/nearby_lamps_notifier.dart';
import '../../application/add_lamp_notifier.dart';
import '../../application/adopt_pulse_controller.dart';

class AdoptConfirmStep extends ConsumerStatefulWidget {
  const AdoptConfirmStep({super.key});

  @override
  ConsumerState<AdoptConfirmStep> createState() => _AdoptConfirmStepState();
}

class _AdoptConfirmStepState extends ConsumerState<AdoptConfirmStep> {
  late final AdoptPulseController _ctrl;
  Object? _error;

  @override
  void initState() {
    super.initState();
    _ctrl = AdoptPulseController(ref.read(bleClientProvider));
    _startPulse();
  }

  @override
  void dispose() {
    // ponytail: fire-and-forget; synchronous part (cancel timer, set _stopped)
    // runs immediately, async write/disconnect drains on next event turn.
    unawaited(_ctrl.stop());
    super.dispose();
  }

  void _startPulse() async {
    if (!mounted) return;
    final deviceId = ref.read(addLampNotifierProvider).deviceId;
    final lamp = ref
        .read(nearbyLampsNotifierProvider)
        .firstWhereOrNull((l) => l.id == deviceId);
    // Pulse the lamp's SHADE colour (rendered on the base strip in the
    // controller) — the shade is the more visible surface, so it reads
    // more clearly as "this is the one".
    final pulseColor = lamp != null
        ? LampColor(
            r: (lamp.shadeRgb >> 16) & 0xFF,
            g: (lamp.shadeRgb >> 8) & 0xFF,
            b: lamp.shadeRgb & 0xFF,
            w: 0,
          )
        : LampColor.black;
    try {
      await _ctrl.start(deviceId, pulseColor);
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
    final critter = critterAssetFor(critterIndex: null, deviceId: deviceId);

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
              SvgPicture.asset(critter, width: 96, height: 96),
              const SizedBox(height: AppSpace.md),
              Text(
                'Found your stray?',
                textAlign: TextAlign.center,
                style: textTheme.headlineSmall,
              ),
              const SizedBox(height: AppSpace.sm),
              Text(
                'The one blinking at you is the stray you tapped. Take it in?',
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
