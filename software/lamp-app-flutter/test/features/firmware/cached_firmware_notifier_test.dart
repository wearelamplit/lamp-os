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
import 'package:lamp_app/features/inventory/domain/inventory_lamp.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';

import '../../_support/signed_firmware.dart';

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
      final cachedImage = await signedFirmwareImage(
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

      final githubImage = await signedFirmwareImage(
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
      final image = await signedFirmwareImage(
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

  _cadenceTests();
}


void _cadenceTests() {
  group('fetch cadence', () {
    late Directory tempDir;
    late Uint8List savedPubKey;
    late SimpleKeyPair keyPair;

    setUp(() async {
      tempDir = await Directory.systemTemp.createTemp('cached_fw_cadence_');
      PathProviderPlatform.instance = _FakeDocsPath(tempDir.path);
      savedPubKey = Uint8List.fromList(firmwarePublicKey);
      keyPair = await Ed25519().newKeyPairFromSeed(List.filled(32, 7));
      firmwarePublicKey.setAll(0, (await keyPair.extractPublicKey()).bytes);
    });

    tearDown(() async {
      firmwarePublicKey.setAll(0, savedPubKey);
      await tempDir.delete(recursive: true);
    });

    Future<Uint8List> image(String lampType, int version) =>
        signedFirmwareImage(keyPair: keyPair, channel: '$lampType-stable', version: version);

    ProviderContainer containerWith(FirmwareReleaseClient client) {
      final c = ProviderContainer(
        overrides: [firmwareReleaseClientProvider.overrideWithValue(client)],
      );
      addTearDown(c.dispose);
      return c;
    }

    Future<void> seedCache(String lampType, int version) async {
      final dir = Directory('${tempDir.path}/firmware-cache');
      await dir.create(recursive: true);
      final bytes = await image(lampType, version);
      final key = '$lampType-stable';
      await File('${dir.path}/lamp-firmware-$key-signed.bin')
          .writeAsBytes(bytes);
      await File('${dir.path}/lamp-firmware-$key.meta.json')
          .writeAsString(jsonEncode(CachedFirmware(
        lampType: lampType,
        channel: FirmwareChannel.stable,
        version: version,
        byteLength: bytes.length,
        fetchedAtMs: 0,
      ).toJson()));
    }

    test('launch fetches each owned variant once; later inventory changes '
        'fetch nothing', () async {
      final stub = _StubReleaseClient(await image('standard', 0x00010203));
      final notifier =
          containerWith(stub).read(cachedFirmwareNotifierProvider.notifier);

      await Future.wait([
        notifier.syncForInventory([_lamp('a', 'standard')]),
        notifier.syncForInventory([_lamp('a', 'standard', lastSeen: 1)]),
      ]);
      final launchFetches = stub.calls.length;
      expect(launchFetches, FirmwareChannel.values.length,
          reason: 'one round at launch: every channel of the one owned variant');
      expect(stub.calls.map((c) => c.lampType).toSet(), {'standard'});

      for (final inventory in [
        [_lamp('a', 'standard', name: 'renamed')],
        [_lamp('a', 'standard', lastSeen: 2)],
        [_lamp('a', 'standard'), _lamp('b', 'standard')],
        [_lamp('b', 'standard')],
        [_lamp('b', 'standard'), _lamp('c', null)],
        <InventoryLamp>[],
      ]) {
        await notifier.syncForInventory(inventory);
      }
      expect(stub.calls.length, launchFetches,
          reason: 'no inventory change may reach the network');
    });

    test('a variant first seen mid-session with nothing cached fetches once',
        () async {
      final stub = _StubReleaseClient(await image('snafu', 0x00010203));
      final notifier =
          containerWith(stub).read(cachedFirmwareNotifierProvider.notifier);

      await notifier.syncForInventory([_lamp('a', 'standard')]);
      final launchFetches = stub.calls.length;

      for (var i = 0; i < 3; i++) {
        await notifier.syncForInventory(
            [_lamp('a', 'standard'), _lamp('b', 'snafu', lastSeen: i)]);
      }
      final midSession = stub.calls.skip(launchFetches).toList();
      expect(midSession.map((c) => c.lampType).toSet(), {'snafu'});
      expect(midSession.length, FirmwareChannel.values.length,
          reason: 'one round for the new variant, never repeated');
    });

    test('mid-session, a variant already cached fetches nothing, even after '
        'a launch with an empty inventory', () async {
      await seedCache('snafu', 0x00010203);
      final stub = _StubReleaseClient(await image('standard', 0x00010203));
      final notifier =
          containerWith(stub).read(cachedFirmwareNotifierProvider.notifier);

      await notifier.syncForInventory(const []);
      await notifier.syncForInventory([_lamp('b', 'snafu')]);
      expect(stub.calls, isEmpty,
          reason: 'snafu is cached, so adopting it waits for the next launch');

      await notifier.syncForInventory([_lamp('b', 'snafu'), _lamp('a', 'standard')]);
      expect(stub.calls.map((c) => c.lampType).toSet(), {'standard'},
          reason: 'standard has nothing cached, so it fetches');
    });

    test('an offline launch keeps the cache, does not retry this session, '
        'and the next launch tries again', () async {
      const cachedVersion = 0x00010400;
      await seedCache('standard', cachedVersion);
      final offline = _OfflineReleaseClient();
      final notifier =
          containerWith(offline).read(cachedFirmwareNotifierProvider.notifier);

      // standard has a cached copy; snafu has none.
      final fleet = [_lamp('a', 'standard'), _lamp('b', 'snafu')];
      await notifier.syncForInventory(fleet);
      final launchAttempts = offline.calls;
      expect(launchAttempts, 2 * FirmwareChannel.values.length);
      for (var i = 0; i < 5; i++) {
        await notifier.syncForInventory(
            [_lamp('a', 'standard', lastSeen: i), _lamp('b', 'snafu')]);
      }
      expect(offline.calls, launchAttempts, reason: 'no retry loop');
      expect(notifier.index['standard-stable']?.version, cachedVersion);

      final relaunched =
          containerWith(offline).read(cachedFirmwareNotifierProvider.notifier);
      await relaunched.syncForInventory(fleet);
      expect(offline.calls, launchAttempts * 2,
          reason: 'a new app launch fetches again');
    });
  });
}

InventoryLamp _lamp(String id, String? lampType,
        {String? name, int? lastSeen}) =>
    InventoryLamp(
      id: id,
      name: name ?? id,
      lampType: lampType,
      lastSeenEpochMs: lastSeen,
    );

class _OfflineReleaseClient extends FirmwareReleaseClient {
  int calls = 0;

  @override
  Future<Uint8List> fetchLatest(FirmwareChannel channel,
      {required String lampType}) async {
    calls++;
    throw const FirmwareDownloadException('network error: offline');
  }
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
  final List<({String lampType, FirmwareChannel channel})> calls = [];
  Iterable<FirmwareChannel> get channelsFetched => calls.map((c) => c.channel);

  @override
  Future<Uint8List> fetchLatest(FirmwareChannel channel,
      {required String lampType}) async {
    calls.add((lampType: lampType, channel: channel));
    return bytes;
  }
}

