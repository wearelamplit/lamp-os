/// App-side mirror of the firmware's `otaAcceptable`
/// (`software/lamp-os/src/components/firmware/ota_channel.cpp`). The lamp
/// enforces this same rule when it receives an OTA offer, so the app must
/// only offer (manual or auto) what the lamp would accept — otherwise a
/// pushed image is silently dropped on the far side.
///
/// [ourChannel]/[offerChannel] are the combined `{type}-{channel}` strings
/// (e.g. `standard-beta`); versions are the packed-int semver used across the
/// app. Intra-channel upgrades require a strictly newer version; a beta lamp
/// promotes to stable at an equal-or-newer version. Everything else — reverse
/// promotion, cross-variant, dev island, unknown channel — is rejected.
bool otaAcceptable(
  String? ourChannel,
  int ourVersion,
  String? offerChannel,
  int offerVersion,
) {
  if (ourChannel == null ||
      offerChannel == null ||
      ourChannel.isEmpty ||
      offerChannel.isEmpty) {
    return false;
  }

  final ourDash = ourChannel.lastIndexOf('-');
  final offerDash = offerChannel.lastIndexOf('-');
  if (ourDash < 0 || offerDash < 0) return false;

  final ourType = ourChannel.substring(0, ourDash);
  final offerType = offerChannel.substring(0, offerDash);
  if (ourType != offerType) return false;

  final ourSuffix = ourChannel.substring(ourDash + 1);
  final offerSuffix = offerChannel.substring(offerDash + 1);

  if (ourSuffix == offerSuffix) return offerVersion > ourVersion;
  if (ourSuffix == 'beta' && offerSuffix == 'stable') {
    return offerVersion >= ourVersion;
  }
  return false;
}
