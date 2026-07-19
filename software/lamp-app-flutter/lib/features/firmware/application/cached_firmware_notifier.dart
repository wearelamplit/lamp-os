import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:path_provider/path_provider.dart';
import 'package:riverpod_annotation/riverpod_annotation.dart';

import '../../inventory/domain/inventory_lamp.dart';
import '../data/cached_firmware.dart';
import '../data/firmware_release_client.dart';
import '../data/firmware_release_client_provider.dart';
import '../data/firmware_signature_verifier.dart';

part 'cached_firmware_notifier.g.dart';

@Riverpod(keepAlive: true, name: 'cachedFirmwareNotifierProvider')
class CachedFirmwareNotifier extends _$CachedFirmwareNotifier {
  static const String _cacheDirName = 'firmware-cache';
  static const String _binSuffix = 'signed.bin';
  static const String _metaSuffix = 'meta.json';

  Directory? _cacheDir;
  bool _busy = false;

  @override
  Future<Map<String, CachedFirmware>> build() async {
    return _loadIndex();
  }

  /// Map keyed by `{lampType}-{channel.name}`. Empty when the cache is
  /// clean or hasn't materialised yet.
  Map<String, CachedFirmware> get index =>
      state.value ?? const <String, CachedFirmware>{};

  Future<Directory> _ensureCacheDir() async {
    if (_cacheDir != null) return _cacheDir!;
    final docs = await getApplicationDocumentsDirectory();
    final dir = Directory('${docs.path}/$_cacheDirName');
    if (!await dir.exists()) {
      await dir.create(recursive: true);
    }
    _cacheDir = dir;
    return dir;
  }

  Future<Map<String, CachedFirmware>> _loadIndex() async {
    final dir = await _ensureCacheDir();
    final out = <String, CachedFirmware>{};
    await for (final entity in dir.list()) {
      if (entity is! File) continue;
      if (!entity.path.endsWith('.$_metaSuffix')) continue;
      try {
        final raw = await entity.readAsString();
        final meta = CachedFirmware.fromJson(
            jsonDecode(raw) as Map<String, dynamic>);
        final binPath = _binPathFor(dir, meta.lampType, meta.channel);
        if (await File(binPath).exists()) {
          out[meta.key] = meta;
        }
      } catch (_) {
        // Skip corrupt sidecars silently. The next sync overwrites.
      }
    }
    return out;
  }

  String _binPathFor(Directory dir, String lampType, FirmwareChannel channel) =>
      '${dir.path}/lamp-firmware-$lampType-${channel.name}-$_binSuffix';

  String _metaPathFor(Directory dir, String lampType, FirmwareChannel channel) =>
      '${dir.path}/lamp-firmware-$lampType-${channel.name}.$_metaSuffix';

  /// Compute the set of {lampType, channel} pairs the user actually
  /// owns. Lamps with no lampType yet (never connected on the new
  /// firmware) are skipped: nothing to fetch until their variant
  /// identity is known.
  Set<({String lampType, FirmwareChannel channel})> _wantedFor(
      List<InventoryLamp> inventory) {
    final out = <({String lampType, FirmwareChannel channel})>{};
    for (final lamp in inventory) {
      final t = lamp.lampType;
      if (t == null || t.isEmpty) continue;
      out.add((lampType: t, channel: firmwareChannelFromString(lamp.fwChannel)));
    }
    return out;
  }

  /// Sync the cache to the user's current inventory. For each wanted
  /// {lampType, channel} pair: fetch from GitHub Releases, verify the
  /// LSIG signature, write bytes + meta sidecar to disk, update state.
  /// Failures are logged + skipped; the previous cached copy survives.
  Future<void> syncForInventory(List<InventoryLamp> inventory) async {
    if (_busy) return; // coalesce concurrent triggers
    _busy = true;
    try {
      final wanted = _wantedFor(inventory);
      if (wanted.isEmpty) return;
      final client = ref.read(firmwareReleaseClientProvider);
      final dir = await _ensureCacheDir();
      final next = Map<String, CachedFirmware>.from(index);
      for (final w in wanted) {
        try {
          final bytes = await client.fetchLatest(w.channel,
              lampType: w.lampType);
          final verified = await verifyFirmwareImage(bytes);
          final binPath = _binPathFor(dir, w.lampType, w.channel);
          final metaPath = _metaPathFor(dir, w.lampType, w.channel);
          await File(binPath).writeAsBytes(bytes, flush: true);
          final meta = CachedFirmware(
            lampType: w.lampType,
            channel: w.channel,
            version: verified.footer.version,
            byteLength: bytes.length,
            fetchedAtMs: DateTime.now().millisecondsSinceEpoch,
          );
          await File(metaPath).writeAsString(jsonEncode(meta.toJson()));
          next[meta.key] = meta;
          state = AsyncData(next);
        } catch (_) {
          // Network / verify failure. Keep the previous entry (if any).
        }
      }
    } finally {
      _busy = false;
    }
  }

  /// Return the cached bytes + meta for a {lampType, channel} pair, or
  /// null if not cached. Used by firmware_notifier.checkForUpdate to
  /// avoid a network round-trip when a fresh-enough copy already exists.
  Future<CachedFirmwareBlob?> bytesFor({
    required String lampType,
    required FirmwareChannel channel,
  }) async {
    final meta = index['$lampType-${channel.name}'];
    if (meta == null) return null;
    try {
      final dir = await _ensureCacheDir();
      final bytes =
          await File(_binPathFor(dir, lampType, channel)).readAsBytes();
      return CachedFirmwareBlob(bytes: Uint8List.fromList(bytes), meta: meta);
    } catch (_) {
      return null;
    }
  }

  /// Wipe a single cache entry (file + sidecar + map slot). Used by the
  /// advanced-mode "remove" affordance.
  Future<void> delete({
    required String lampType,
    required FirmwareChannel channel,
  }) async {
    final key = '$lampType-${channel.name}';
    final meta = index[key];
    if (meta == null) return;
    final dir = await _ensureCacheDir();
    try {
      await File(_binPathFor(dir, lampType, channel)).delete();
    } catch (_) {}
    try {
      await File(_metaPathFor(dir, lampType, channel)).delete();
    } catch (_) {}
    final next = Map<String, CachedFirmware>.from(index)..remove(key);
    state = AsyncData(next);
  }
}
