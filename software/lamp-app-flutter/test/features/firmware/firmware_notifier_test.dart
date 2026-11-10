import 'dart:async';
import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:lamp_app/core/ble/ble_client_provider.dart';
import 'package:lamp_app/core/ble/uuids.dart';
import 'package:lamp_app/features/firmware/application/cached_firmware_notifier.dart';
import 'package:lamp_app/features/firmware/application/firmware_notifier.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_pubkey.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';
import 'package:lamp_app/features/firmware/domain/firmware_protocol.dart';
import 'package:lamp_app/features/firmware/domain/firmware_state.dart';

import '../../_support/fw_frames.dart';
import '../../_support/in_memory_ble_client.dart';
import '../../_support/signed_firmware.dart';

const _id = 'lamp-push';
const _version = 0x00010205;

/// A cache that holds one image.
class _OneImage extends CachedFirmwareNotifier {
  _OneImage(this.image);
  final Uint8List image;

  static const _meta = CachedFirmware(
    lampType: 'standard',
    channel: FirmwareChannel.beta,
    version: _version,
    byteLength: 0,
    fetchedAtMs: 0,
  );

  @override
  Future<Map<String, CachedFirmware>> build() async => {_meta.key: _meta};

  @override
  Future<CachedFirmwareBlob?> bytesFor({
    required String lampType,
    required FirmwareChannel channel,
  }) async => CachedFirmwareBlob(bytes: image, meta: _meta);
}

Future<void> _until(bool Function() done) async {
  for (var i = 0; i < 200 && !done(); i++) {
    await Future<void>.delayed(const Duration(milliseconds: 10));
  }
}

void main() {
  late Uint8List savedKey;
  setUp(() => savedKey = Uint8List.fromList(firmwarePublicKey));
  tearDown(() => firmwarePublicKey.setAll(0, savedKey));

  test('a push streams, then finalizes once its last chunk is out, then '
      'succeeds', () async {
    final keyPair = await Ed25519().newKeyPairFromSeed(List.filled(32, 7));
    firmwarePublicKey.setAll(0, (await keyPair.extractPublicKey()).bytes);
    final image = await signedFirmwareImage(
      keyPair: keyPair,
      channel: 'standard-beta',
      version: _version,
      signedRegionLen: 2000,
    );
    final ble = InMemoryBleClient();
    await ble.connect(_id);
    final c = ProviderContainer(
      overrides: [
        bleClientProvider.overrideWithValue(ble),
        cachedFirmwareNotifierProvider.overrideWith(() => _OneImage(image)),
      ],
    );
    addTearDown(c.dispose);
    final states = <FirmwareState>[];
    c.listen(firmwareNotifierProvider(_id), (_, s) => states.add(s));

    unawaited(c
        .read(firmwareNotifierProvider(_id).notifier)
        .installFromCache(lampType: 'standard', channel: FirmwareChannel.beta));
    List<Uint8List> offers() => ble.writesTo(_id, BleUuids.fwControl);
    await _until(() => offers().isNotEmpty);
    final offerSeq =
        ByteData.view(offers().first.buffer).getUint16(4, Endian.little);
    ble.simulateNotify(
      _id,
      BleUuids.controlService,
      BleUuids.fwControl,
      makeAccept(
        seq: 1,
        offerSeq: offerSeq,
        version: _version,
        status: FwAcceptStatus.accept,
      ),
    );
    await _until(() => states.any((s) => s is FirmwareFinalizing));
    ble.simulateNotify(
      _id,
      BleUuids.controlService,
      BleUuids.fwControl,
      makeResult(seq: 2, status: FwResultStatus.success, version: _version),
    );
    await _until(() => states.last is FirmwareSucceeded);

    final streamed = states.whereType<FirmwareStreaming>().length;
    expect(streamed, greaterThan(1));
    final finalizing = states.indexWhere((s) => s is FirmwareFinalizing);
    expect(finalizing, greaterThan(states.lastIndexWhere((s) => s is FirmwareStreaming)));
    expect(states.last, isA<FirmwareSucceeded>());
  });
}
