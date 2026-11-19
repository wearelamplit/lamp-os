import 'dart:convert';
import 'dart:typed_data';

import 'tuple_sampler.dart';
import 'wisp_source_mode.dart';

/// The wisp's settings as its `wispConfig` broadcast carries them, read from
/// the lamp's `wispconfig` page section. The wisp sends every field every
/// time, so a parsed [WispConfig] is complete: nothing here is a default
/// standing in for a missing value. A field a newer wisp added is nullable,
/// and null means the wisp predates it.
class WispConfig {
  const WispConfig({
    required this.ledType,
    required this.pixelCount,
    required this.brightness,
    required this.driftIntervalMs,
    required this.driftFadePct,
    required this.shuffleSeed,
    required this.opSeq,
    this.ip,
    this.paletteStyle,
    this.oscMode,
    this.oscSlot,
    this.oscAddr,
  });

  final String ledType;
  final int pixelCount;
  final int brightness;
  final int driftIntervalMs;
  final int driftFadePct;
  final int shuffleSeed;
  final int opSeq;

  /// Where an OSC sender reaches the wisp on UDP [kOscPort]: `""` while it
  /// has no network address.
  final String? ip;

  static const int kOscPort = 9000;

  /// How lamps take colours from the palette (`swatches`).
  final PaletteStyle? paletteStyle;

  /// Which OSC sender the wisp listens to (`oscMode`).
  final OscMode? oscMode;

  /// The Maestro palette slot the wisp reads (`oscSlot`), kept while it
  /// listens to another sender.
  final int? oscSlot;

  /// The Custom OSC address the wisp stores (`oscAddr`), kept while it listens
  /// to another sender, and `""` while it has none. Null from a wisp that
  /// predates Custom.
  final String? oscAddr;

  /// Parse the section bytes. Null for an empty read (a lamp that predates
  /// the section), `"{}"` (no config cached yet), a payload that is not a
  /// `wispConfig`, or one missing any field every wisp sends.
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
    final ip = decoded['ip'];
    final swatches = decoded['swatches'];
    final oscMode = decoded['oscMode'];
    final oscSlot = decoded['oscSlot'];
    final oscAddr = decoded['oscAddr'];
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
      ip: ip is String ? ip : null,
      paletteStyle: swatches is bool
          ? (swatches ? PaletteStyle.swatches : PaletteStyle.gradient)
          : null,
      oscMode: parseOscMode(oscMode),
      oscSlot: parseMaestroSlot(oscSlot),
      oscAddr: oscAddr is String ? oscAddr : null,
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
      ip: ip,
      paletteStyle: paletteStyle,
      oscMode: oscMode,
      oscSlot: oscSlot,
      oscAddr: oscAddr,
    );
  }
}
