/// Whimsical copy for a lamp that is sourcing firmware to a peer over the
/// mesh. Such a lamp is hard to reach over BLE (the OTA bursts starve the
/// connect), so the app explains the wait in the lamp's voice instead of a
/// generic error. `targetName` names the receiver when it can be resolved
/// (the connected lamp's nearby data); the scan-time advertisement carries
/// only the busy flag, so the picker falls back to the generic form.
String otaBusyMessage({required String lampName, String? targetName}) {
  final pupil = (targetName != null && targetName.isNotEmpty)
      ? targetName
      : 'a lamp';
  return '$lampName is busy teaching $pupil new tricks 🎓';
}
