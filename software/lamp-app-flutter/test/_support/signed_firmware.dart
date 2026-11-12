import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';
import 'package:lamp_app/features/firmware/data/firmware_pubkey.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';

Uint8List lsigFooterBytes({
  required String channel,
  required int version,
  required int signedRegionLen,
  required Uint8List signature,
}) {
  final out = Uint8List(lsigFooterLen);
  final view = ByteData.view(out.buffer);
  view.setUint8(0, 0x4C);
  view.setUint8(1, 0x53);
  view.setUint8(2, 0x49);
  view.setUint8(3, 0x47);
  final channelBytes = channel.codeUnits;
  for (var i = 0; i < lsigChannelLen; ++i) {
    view.setUint8(
        lsigChannelOffset + i, i < channelBytes.length ? channelBytes[i] : 0);
  }
  view.setUint32(lsigVersionOffset, version, Endian.little);
  view.setUint32(lsigSignedRegLenOffset, signedRegionLen, Endian.little);
  for (var i = 0; i < lsigSignatureLen; ++i) {
    view.setUint8(lsigSignatureOffset + i, signature[i]);
  }
  return out;
}

/// Builds a real signed image: SHA-256 over a synthetic signed region,
/// Ed25519-signed with [keyPair] (whose public key must be installed into
/// [firmwarePublicKey] for `verifyFirmwareImage` to accept it).
Future<Uint8List> signedFirmwareImage({
  required SimpleKeyPair keyPair,
  required String channel,
  required int version,
  int signedRegionLen = 512,
}) async {
  final region =
      Uint8List.fromList(List.generate(signedRegionLen, (i) => i & 0xFF));
  final digest = await Sha256().hash(region);
  final signature =
      await Ed25519().sign(digest.bytes, keyPair: keyPair);
  final footer = lsigFooterBytes(
    channel: channel,
    version: version,
    signedRegionLen: signedRegionLen,
    signature: Uint8List.fromList(signature.bytes),
  );
  return Uint8List.fromList([...region, ...footer]);
}
