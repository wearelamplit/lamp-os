import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/widgets/app_snackbar.dart';
import '../../control/application/control_notifier.dart';
import '../application/firmware_notifier.dart';
import '../application/quiet_after_push.dart';
import '../domain/firmware_state.dart';
import 'firmware_copy.dart';

/// Tells the owner how a push to [lampId] ended, once: that the lamp learned
/// its new tricks when it is back on the new version, or that it didn't when
/// a failed push's quiet hold ends. A declined or cancelled offer says
/// nothing, and the next push supersedes a line still waiting.
class FirmwarePushOutcome extends ConsumerStatefulWidget {
  const FirmwarePushOutcome({
    super.key,
    required this.lampId,
    required this.child,
  });

  final String lampId;
  final Widget child;

  @override
  ConsumerState<FirmwarePushOutcome> createState() =>
      _FirmwarePushOutcomeState();
}

class _FirmwarePushOutcomeState extends ConsumerState<FirmwarePushOutcome> {
  /// The version the lamp reboots into after a good push.
  int? _learnedVersion;
  bool _missed = false;

  String get _name => FirmwareCopy.nameOf(
    ref.read(controlNotifierProvider(widget.lampId)).value,
  );

  void _onFirmware(FirmwareState? previous, FirmwareState next) {
    if (next.isBusy) {
      _learnedVersion = null;
      _missed = false;
      return;
    }
    if (!(previous?.isPushing ?? false)) return;
    switch (next) {
      case FirmwareSucceeded(:final footer, alreadyCurrent: false):
        _learnedVersion = footer.version;
      case FirmwareFailed(:final failure)
          when !failure.declined && failure != FirmwareFailure.cancelled:
        _missed = true;
        // The lamp's ControlNotifier starts the quiet hold on this same
        // transition; look once it has.
        scheduleMicrotask(_sayMissed);
      default:
    }
  }

  void _sayMissed() {
    if (!mounted || !_missed) return;
    if (ref.read(quietAfterPushProvider(widget.lampId))) return;
    AppSnackbar.error(context, FirmwareCopy.missed(_name));
  }

  void _onLampVersion(int? fwVersion) {
    final learned = _learnedVersion;
    if (learned == null || fwVersion != learned) return;
    AppSnackbar.info(context, FirmwareCopy.learned(_name));
  }

  @override
  Widget build(BuildContext context) {
    final id = widget.lampId;
    ref.listen(firmwareNotifierProvider(id), _onFirmware);
    ref.listen(quietAfterPushProvider(id), (_, quiet) {
      if (!quiet) _sayMissed();
    });
    ref.listen(
      controlNotifierProvider(id).select((s) => s.value?.lamp.fwVersion),
      (_, version) => _onLampVersion(version),
    );
    return widget.child;
  }
}
