import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../control/application/control_notifier.dart';
import '../../control/application/control_state.dart';
import 'cached_firmware_notifier.dart';
import 'firmware_gate.dart';
import 'firmware_notifier.dart';

part 'firmware_auto_installer.g.dart';

/// Auto-sends a newer cached image the moment a lamp connects, so the user
/// never taps Install. Kept alive by LampShell's watch; disposed (latch
/// reset) when the shell unmounts, giving a fresh evaluation on the next
/// visit. Reset-on-disconnect lets a dropped/failed push retry on reconnect;
/// the version gate makes a rebooted lamp ineligible on its own.
@Riverpod(name: 'firmwareAutoInstallerProvider')
class FirmwareAutoInstaller extends _$FirmwareAutoInstaller {
  int? _latchedVersion;

  @override
  void build(String deviceId) {
    ref.listen(
      controlNotifierProvider(deviceId),
      (_, next) => _evaluate(deviceId, next.value),
      fireImmediately: true,
    );
    ref.listen(
      cachedFirmwareNotifierProvider,
      (_, _) =>
          _evaluate(deviceId, ref.read(controlNotifierProvider(deviceId)).value),
      fireImmediately: true,
    );
  }

  void _evaluate(String deviceId, ControlState? state) {
    if (state == null || !state.connected) {
      _latchedVersion = null;
      return;
    }
    final target = firmwareAutoInstallTarget(
      connected: state.connected,
      lampType: state.lamp.lampType,
      fwVersion: state.lamp.fwVersion,
      fwChannel: state.lamp.fwChannel,
      cache: ref.read(cachedFirmwareNotifierProvider).value,
      latchedVersion: _latchedVersion,
    );
    if (target == null) return;
    _latchedVersion = target.version;
    ref.read(firmwareNotifierProvider(deviceId).notifier).installFromCache(
          lampType: target.lampType,
          channel: target.channel,
        );
  }
}
