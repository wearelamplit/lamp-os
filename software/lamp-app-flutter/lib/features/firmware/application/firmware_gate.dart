import '../data/cached_firmware.dart';
import '../domain/ota_acceptance.dart';

enum FirmwareRowAction { install, upToDate, notThisLamp, versionUnknown }

/// Gate a cached entry against the lamp with the firmware's own OTA
/// acceptance rule (`otaAcceptable`): [install] exactly when the lamp would
/// accept the offer. [lampChannel] is the lamp's combined `{type}-{channel}`
/// wire string; [lampType] only refines the rejected label (wrong variant vs
/// already current).
FirmwareRowAction firmwareRowActionFor({
  required CachedFirmware entry,
  required String? lampType,
  required String? lampChannel,
  required int? lampFwVersion,
}) {
  if (lampFwVersion == null || lampChannel == null || lampChannel.isEmpty) {
    return FirmwareRowAction.versionUnknown;
  }
  final offerChannel = '${entry.lampType}-${entry.channel.name}';
  if (otaAcceptable(lampChannel, lampFwVersion, offerChannel, entry.version)) {
    return FirmwareRowAction.install;
  }
  if (entry.lampType != lampType) return FirmwareRowAction.notThisLamp;
  return FirmwareRowAction.upToDate;
}

/// Which cached image to auto-push to a connected lamp, or null for none.
/// Scans the cache for an entry the lamp would accept per `otaAcceptable`, so
/// a beta lamp promotes to a stable entry at equal-or-newer version.
/// `latchedVersion` is the version already attempted while continuously
/// connected; the caller clears it on disconnect so a dropped/failed push
/// retries on reconnect, and the acceptance rule makes a rebooted lamp
/// ineligible on its own.
CachedFirmware? firmwareAutoInstallTarget({
  required bool connected,
  required String? lampType,
  required int? fwVersion,
  required String? fwChannel,
  required Map<String, CachedFirmware>? cache,
  required int? latchedVersion,
}) {
  if (!connected ||
      lampType == null ||
      fwVersion == null ||
      fwChannel == null ||
      fwChannel.isEmpty ||
      cache == null) {
    return null;
  }
  for (final entry in cache.values) {
    if (entry.lampType != lampType) continue;
    final offerChannel = '${entry.lampType}-${entry.channel.name}';
    if (!otaAcceptable(fwChannel, fwVersion, offerChannel, entry.version)) {
      continue;
    }
    if (latchedVersion == entry.version) return null;
    return entry;
  }
  return null;
}
