// ignore_for_file: depend_on_referenced_packages
// path_provider_platform_interface: transitive via path_provider, used here
// to fake the docs directory.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:cryptography/cryptography.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/firmware/application/cached_firmware_notifier.dart';
import 'package:lamp_app/features/firmware/data/cached_firmware.dart';
import 'package:lamp_app/features/firmware/data/firmware_pubkey.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client.dart';
import 'package:lamp_app/features/firmware/data/firmware_release_client_provider.dart';
import 'package:lamp_app/features/firmware/domain/lsig_footer.dart';
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';

void main() {
  group('shouldReplaceCachedFirmware', () {
    test('no cached entry → replace', () {
      expect(shouldReplaceCachedFirmware(null, 5), isTrue);
    });

    test('cached older than github → replace', () {
      expect(shouldReplaceCachedFirmware(4, 5), isTrue);
    });

    test('cached equal to github → replace', () {
      expect(shouldReplaceCachedFirmware(5, 5), isTrue);
    });

    test('cached newer than github → keep cached', () {
      expect(shouldReplaceCachedFirmware(6, 5), isFalse);
    });
  });

  group('syncForInventory async-load race', () {
    late Directory tempDir;
    late Uint8List savedPubKey;

    setUp(() async {
      tempDir = await Directory.systemTemp.createTemp('cached_fw_test_');
      PathProviderPlatform.instance = _FakeDocsPath(tempDir.path);
      savedPubKey = Uint8List.fromList(firmwarePublicKey);
    });

    tearDown(() async {
      firmwarePublicKey.setAll(0, savedPubKey);
      await tempDir.delete(recursive: true);
    });

    test(
        'a sync started before the cache index finishes loading does not '
        'let an older GitHub release clobber an already-cached newer build',
        () async {
      final keyPair = await Ed25519().newKeyPairFromSeed(List.filled(32, 7));
      final publicKey = await keyPair.extractPublicKey();
      firmwarePublicKey.setAll(0, publicKey.bytes);

      const lampType = 'standard';
      // Both channels are fetched per owned variant; the pre-seeded stable
      // entry (below) shares the stable key with the stable fetch.
      const channel = FirmwareChannel.stable;
      const cachedVersion = 0x00010400; // 1.4.0, already cached on disk
      const githubVersion = 0x00010200; // 1.2.0, what GitHub offers

      // Seed the on-disk cache as if a newer build was pushed manually
      // before this run (e.g. a prior app session).
      final cacheDir = Directory('${tempDir.path}/firmware-cache');
      await cacheDir.create(recursive: true);
      final cachedImage = await _signedImage(
        keyPair: keyPair,
        channel: '$lampType-${channel.name}',
        version: cachedVersion,
      );
      final key = '$lampType-${channel.name}';
      await File('${cacheDir.path}/lamp-firmware-$key-signed.bin')
          .writeAsBytes(cachedImage);
      final meta = CachedFirmware(
        lampType: lampType,
        channel: channel,
        version: cachedVersion,
        byteLength: cachedImage.length,
        fetchedAtMs: 0,
      );
      await File('${cacheDir.path}/lamp-firmware-$key.meta.json')
          .writeAsString(jsonEncode(meta.toJson()));

      final githubImage = await _signedImage(
        keyPair: keyPair,
        channel: '$lampType-${channel.name}',
        version: githubVersion,
      );
      final container = ProviderContainer(
        overrides: [
          firmwareReleaseClientProvider
              .overrideWithValue(_StubReleaseClient(githubImage)),
        ],
      );
      addTearDown(container.dispose);

      final notifier = container.read(cachedFirmwareNotifierProvider.notifier);
      // Fire the sync immediately, without awaiting the provider's initial
      // build — the exact race a relaunch hits when inventory sync kicks
      // off before the disk scan in _loadIndex() resolves.
      await notifier.syncForInventory(const [
        InventoryLamp(
          id: 'lamp-A',
          name: 'lamp-A',
          lampType: lampType,
          fwChannel: '$lampType-beta',
        ),
      ]);

      final index = await container.read(cachedFirmwareNotifierProvider.future);
      expect(
        index[key]?.version,
        equals(cachedVersion),
        reason: 'an older GitHub release must not overwrite an '
            'already-cached newer build',
      );
    });

    test('syncs both the stable and beta channel for an owned variant',
        () async {
      final keyPair = await Ed25519().newKeyPairFromSeed(List.filled(32, 7));
      final publicKey = await keyPair.extractPublicKey();
      firmwarePublicKey.setAll(0, publicKey.bytes);

      const lampType = 'standard';
      final image = await _signedImage(
        keyPair: keyPair,
        channel: '$lampType-stable',
        version: 0x00010200,
      );
      final stub = _StubReleaseClient(image);
      final container = ProviderContainer(
        overrides: [
          firmwareReleaseClientProvider.overrideWithValue(stub),
        ],
      );
      addTearDown(container.dispose);

      await container
          .read(cachedFirmwareNotifierProvider.notifier)
          .syncForInventory(const [
        InventoryLamp(
          id: 'lamp-A',
          name: 'lamp-A',
          lampType: lampType,
          fwChannel: '$lampType-beta',
        ),
      ]);

      expect(
        stub.channelsFetched.toSet(),
        equals({FirmwareChannel.stable, FirmwareChannel.beta}),
        reason: 'both channels must be fetched so a mixed fleet can update '
            'from either',
      );
    });
  });
}

class _FakeDocsPath extends PathProviderPlatform {
  _FakeDocsPath(this.path);
  final String path;

  @override
  Future<String?> getApplicationDocumentsPath() async => path;
}

class _StubReleaseClient extends FirmwareReleaseClient {
  _StubReleaseClient(this.bytes);
  final Uint8List bytes;
  final List<FirmwareChannel> channelsFetched = [];

  @override
  Future<Uint8List> fetchLatest(FirmwareChannel channel,
      {required String lampType}) async {
    channelsFetched.add(channel);
    return bytes;
  }
}

Uint8List _lsigFooter({
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
Future<Uint8List> _signedImage({
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
  final footer = _lsigFooter(
    channel: channel,
    version: version,
    signedRegionLen: signedRegionLen,
    signature: Uint8List.fromList(signature.bytes),
  );
  return Uint8List.fromList([...region, ...footer]);
}
