import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/widgets/app_snackbar.dart';
import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import '../application/firmware_notifier.dart';
import '../application/quiet_after_push.dart';
import '../domain/firmware_state.dart';
import 'firmware_copy.dart';

/// Tells the owner how a push to [lampId] ended, once: that the lamp learned
/// its new tricks when it reports the pushed version, or that it didn't once
/// a failed push's quiet hold has ended and the connected lamp reports
/// another version. A push that lost the link waits for the next connect to
/// read the lamp, since the lamp may have installed it anyway. A declined or
/// cancelled offer says nothing, and the next push supersedes a line still
/// waiting.
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
  /// The version of the push on the wire, kept after it ends unless the lamp
  /// turned it down.
  int? _pushedVersion;
  bool _missed = false;

  /// The push lost the link, so the lamp's version is unknown until the
  /// next connect reads it.
  bool _awaitingConnect = false;

  ControlState? get _lamp =>
      ref.read(controlNotifierProvider(widget.lampId)).value;

  String get _name => FirmwareCopy.nameOf(_lamp?.lamp.name);

  void _onFirmware(FirmwareState? previous, FirmwareState next) {
    if (next.isBusy) {
      _missed = false;
      _awaitingConnect = false;
      _pushedVersion = switch (next) {
        FirmwareOfferSent(:final footer) ||
        FirmwareStreaming(:final footer) ||
        FirmwareFinalizing(:final footer) => footer.version,
        _ => null,
      };
      return;
    }
    final pushed = switch (previous) {
      FirmwareOfferSent(:final footer) ||
      FirmwareStreaming(:final footer) ||
      FirmwareFinalizing(:final footer) => footer.version,
      _ => null,
    };
    if (pushed == null) return;
    _pushedVersion = pushed;
    switch (next) {
      case FirmwareSucceeded(alreadyCurrent: false):
        break;
      case FirmwareFailed(:final failure)
          when !failure.declined && failure != FirmwareFailure.cancelled:
        if (_lamp?.lamp.fwVersion case final v? when v == _pushedVersion) {
          return;
        }
        _missed = true;
        _awaitingConnect = failure == FirmwareFailure.linkLost;
        // The lamp's ControlNotifier starts the quiet hold on this same
        // transition; look once it has.
        scheduleMicrotask(_sayMissed);
      default:
        _pushedVersion = null;
    }
  }

  void _sayMissed() {
    if (!mounted || !_missed || _awaitingConnect) return;
    if (ref.read(quietAfterPushProvider(widget.lampId))) return;
    if (_lamp?.connected != true) return;
    _missed = false;
    AppSnackbar.error(context, FirmwareCopy.missed(_name));
  }

  void _onLamp((int?, bool)? previous, (int?, bool) next) {
    final (version, connected) = next;
    if (connected && !(previous?.$2 ?? false)) _awaitingConnect = false;
    if (version != null &&
        version != previous?.$1 &&
        version == _pushedVersion) {
      _missed = false;
      AppSnackbar.info(context, FirmwareCopy.learned(_name));
    }
    _sayMissed();
  }

  @override
  Widget build(BuildContext context) {
    final id = widget.lampId;
    ref.listen(firmwareNotifierProvider(id), _onFirmware);
    ref.listen(quietAfterPushProvider(id), (_, quiet) {
      if (!quiet) _sayMissed();
    });
    ref.listen(
      controlNotifierProvider(id).select(
        (s) => (s.value?.lamp.fwVersion, s.value?.connected ?? false),
      ),
      _onLamp,
    );
    return widget.child;
  }
}
