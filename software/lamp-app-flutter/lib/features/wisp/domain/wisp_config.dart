import 'dart:convert';
import 'dart:typed_data';

/// The wisp's settings as its `wispConfig` broadcast carries them, read from
/// the lamp's `wispconfig` page section. The wisp sends every field every
/// time, so a parsed [WispConfig] is complete: nothing here is a default
/// standing in for a missing value.
class WispConfig {
  const WispConfig({
    required this.ledType,
    required this.pixelCount,
    required this.brightness,
    required this.driftIntervalMs,
    required this.driftFadePct,
    required this.shuffleSeed,
    required this.opSeq,
  });

  final String ledType;
  final int pixelCount;
  final int brightness;
  final int driftIntervalMs;
  final int driftFadePct;
  final int shuffleSeed;
  final int opSeq;

  /// Parse the section bytes. Null for an empty read (a lamp that predates
  /// the section), `"{}"` (no config cached yet), a payload that is not a
  /// `wispConfig`, or one missing any field.
  static WispConfig? fromBytes(Uint8List bytes) {
    if (bytes.isEmpty) return null;
    final Object? decoded;
    try {
      decoded = jsonDecode(utf8.decode(bytes));
    } catch (_) {
      return null;
    }
    if (decoded is! Map<String, dynamic>) return null;
    if (decoded['char'] != 'wispConfig') return null;
    final ledType = decoded['ledType'];
    final px = decoded['px'];
    final brightness = decoded['brightness'];
    final driftIntervalMs = decoded['driftIntervalMs'];
    final driftFadePct = decoded['driftFadePct'];
    final shuffleSeed = decoded['shuffleSeed'];
    final opSeq = decoded['opSeq'];
    if (ledType is! String ||
        px is! int ||
        brightness is! int ||
        driftIntervalMs is! int ||
        driftFadePct is! int ||
        shuffleSeed is! int ||
        opSeq is! int) {
      return null;
    }
    return WispConfig(
      ledType: ledType,
      pixelCount: px,
      brightness: brightness,
      driftIntervalMs: driftIntervalMs,
      driftFadePct: driftFadePct,
      shuffleSeed: shuffleSeed,
      opSeq: opSeq,
    );
  }

  WispConfig copyWith({
    String? ledType,
    int? pixelCount,
    int? brightness,
    int? driftIntervalMs,
    int? driftFadePct,
  }) {
    return WispConfig(
      ledType: ledType ?? this.ledType,
      pixelCount: pixelCount ?? this.pixelCount,
      brightness: brightness ?? this.brightness,
      driftIntervalMs: driftIntervalMs ?? this.driftIntervalMs,
      driftFadePct: driftFadePct ?? this.driftFadePct,
      shuffleSeed: shuffleSeed,
      opSeq: opSeq,
    );
  }
}
