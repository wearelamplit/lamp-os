import 'dart:typed_data';

import 'firmware_release_client.dart';

/// One cached signed-firmware artifact on disk. The key is the
/// `{lampType}-{channel.name}` pair, same shape as the LSIG footer's
/// channel field. The `.bin` path is derived from the current cache
/// directory plus this key, not stored, so the cache survives an iOS
/// container-UUID change. `version` is the packed semver parsed from the
/// verified LSIG footer; `fetchedAtMs` is the wall-clock timestamp of the
/// download (epoch millis).
class CachedFirmware {
  const CachedFirmware({
    required this.lampType,
    required this.channel,
    required this.version,
    required this.byteLength,
    required this.fetchedAtMs,
  });

  final String lampType;
  final FirmwareChannel channel;
  final int version;
  final int byteLength;
  final int fetchedAtMs;

  String get key => '$lampType-${channel.name}';

  int get major => (version >> 16) & 0xFF;
  int get minor => (version >> 8) & 0xFF;
  int get patch => version & 0xFF;
  String get versionString => '$major.$minor.$patch';

  Map<String, dynamic> toJson() => {
        'lampType': lampType,
        'channel': channel.name,
        'version': version,
        'byteLength': byteLength,
        'fetchedAtMs': fetchedAtMs,
      };

  factory CachedFirmware.fromJson(Map<String, dynamic> json) => CachedFirmware(
        lampType: json['lampType'] as String,
        channel: FirmwareChannel.values
            .firstWhere((c) => c.name == json['channel'] as String),
        version: (json['version'] as num).toInt(),
        byteLength: (json['byteLength'] as num).toInt(),
        fetchedAtMs: (json['fetchedAtMs'] as num).toInt(),
      );
}

/// Loaded bytes + parsed metadata for an installable cache entry. The
/// firmware_notifier picks this up via CachedFirmwareNotifier.bytesFor.
class CachedFirmwareBlob {
  const CachedFirmwareBlob({required this.bytes, required this.meta});
  final Uint8List bytes;
  final CachedFirmware meta;
}
