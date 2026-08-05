import 'dart:convert';
import 'dart:typed_data';

import 'package:collection/collection.dart';

import '../../control/domain/lamp_color.dart';
import 'wisp_source_mode.dart';
import 'zone_source.dart';

const _observedZonesEq = ListEquality<int>();

/// Default Off-mode color. Matches the wisp firmware's NVS-default for
/// `offColor` (warm candle-amber); also the fallback when the wispStatus
/// payload pre-dates the offColor field.
const LampColor _defaultOffColor = LampColor(r: 255, g: 150, b: 50, w: 0);

/// Parsed `CHAR_WISP_STATUS` payload. The lamp serves a merged JSON
/// combining the last `wispStatus` MSG_CONTROL_OP broadcast (from the
/// wisp) with the last MSG_WISP_HELLO data. When no wisp has been seen
/// the lamp returns `"{}"`, which round-trips into [WispStatus.empty].
///
/// Some hello fields may be absent when a status broadcast lands before
/// the first hello (or vice-versa); every getter handles missing keys
/// gracefully.
class WispStatus {
  const WispStatus({
    this.currentZone,
    this.zoneSource = ZoneSource.none,
    this.observedZones = const <int>[],
    this.wifiConnected = false,
    this.wifiChannelMismatch = false,
    this.wifiApChannel = 0,
    this.auroraConnected = false,
    this.paletteIdPrefix = '',
    this.lastSeenMs,
    this.wispMac,
    this.wispVersion,
    this.helloFlags,
    this.helloPaletteIdPrefix,
    this.helloLastSeenMs,
    this.statusLastSeenMs,
    this.source = WispSourceMode.aurora,
    this.offColor = _defaultOffColor,
    this.controllingBase = false,
    this.controllingShade = false,
    this.baseWispColor,
    this.shadeWispColor,
    this.shuffleSeed = 0,
    this.driftIntervalMs = 120000,
    this.driftFadePct = 50,
    this.name = '',
    this.hasPassword = false,
    this.ledType = 'GRB',
    this.pixelCount = 30,
    this.opSeq = 0,
    this.brightness = 100,
  });

  /// Sentinel for "no wisp has been heard on this lamp yet" (lamp
  /// returned `"{}"`) and the parse-failure fallback.
  static const empty = WispStatus();

  /// Zone the wisp is currently following (i.e. the zone whose palette
  /// it forwards). `null` when [zoneSource] is `"none"`.
  final int? currentZone;

  /// Where [currentZone] came from. See [ZoneSource] for the enum semantics.
  final ZoneSource zoneSource;

  /// Zone IDs the wisp has heard recently on the mesh. Drives the
  /// zone-picker chip list in the UI.
  final List<int> observedZones;

  /// Wisp's WiFi radio link to the home AP.
  final bool wifiConnected;

  /// True when the wisp rejected the home AP because it was on a channel
  /// other than the mesh's channel 6. The single radio can't span two
  /// channels, so the wisp stays on the mesh and reports this instead.
  /// [wifiConnected] is false whenever this is true.
  final bool wifiChannelMismatch;

  /// The channel the home AP was on when [wifiChannelMismatch] tripped.
  /// 0 when absent/unknown.
  final int wifiApChannel;

  /// Wisp's TCP/WS link to Aurora. False whenever WiFi is down; can
  /// also be false while WiFi is up but Aurora is unreachable.
  final bool auroraConnected;

  /// First 8 hex chars of the palette ID the wisp last published.
  /// Empty string when the wisp hasn't published a palette yet.
  final String paletteIdPrefix;

  /// Wisp's local `millis()` at the moment it serialised this status.
  /// NOT comparable across reboots; the UI shows "last seen" relative
  /// to the phone's local clock instead (computed at notify time).
  final int? lastSeenMs;

  /// Wisp's mesh MAC address, uppercase colon-separated (e.g.
  /// `"AA:BB:CC:DD:EE:FF"`). `null` when no wisp has been observed.
  final String? wispMac;

  /// Wisp firmware version (semantic-versioned u32 in hello).
  final int? wispVersion;

  /// Hello capability flags. Bit layout owned by the wisp side.
  final int? helloFlags;

  /// 8-char palette prefix carried in the hello payload (may differ
  /// from [paletteIdPrefix] if a status arrived more recently than
  /// the last hello).
  final String? helloPaletteIdPrefix;

  /// Lamp-side `lastSeenMs` for the hello half of the merge.
  final int? helloLastSeenMs;

  /// Lamp-side `lastSeenMs` for the status half of the merge.
  final int? statusLastSeenMs;

  /// True iff this lamp is currently following a wisp paint on the Base
  /// surface, i.e. its baseColorOverride is FadingIn / Holding /
  /// Restoring AND the most recent apply that took effect was
  /// Wisp-sourced. Drives the will-o'-wisp indicator in the control
  /// screen header.
  final bool controllingBase;

  /// Same as [controllingBase] but for the Shade surface.
  final bool controllingShade;

  /// First stop of the most recent wisp paint on Base. `null` until the
  /// first wisp paint has landed on this lamp's Base. Used to colour one
  /// half of the indicator.
  final LampColor? baseWispColor;

  /// Same as [baseWispColor] for the Shade surface.
  final LampColor? shadeWispColor;

  /// Current shuffle seed. Mixed into the TupleSampler hash salts so the
  /// app preview stays in lock-step with the wisp's color assignments.
  /// Defaults to 0 (matches the firmware default and pre-feature wisps).
  final int shuffleSeed;

  /// How often the wisp advances the drift color, in milliseconds.
  /// Defaults to 120000 (2 min) matching the firmware default.
  final int driftIntervalMs;

  /// Fade depth for drift transitions, as a percentage of the color delta.
  /// Defaults to 50 matching the firmware default.
  final int driftFadePct;

  /// Wisp's human-readable name, set via the `setName` wispOp.
  /// Empty string on factory-fresh wisps or payloads pre-dating this field.
  final String name;

  /// True when the wisp has a password set.
  final bool hasPassword;

  /// LED strip byte order reported by the wisp (`'GRBW'`, `'GRB'`, `'BGR'`).
  /// Absent on legacy wisps; defaults to `'GRB'` matching the firmware default.
  final String ledType;

  /// Number of pixels in the wisp's LED ring. Absent on legacy wisps;
  /// defaults to 30 matching the firmware default.
  final int pixelCount;

  /// Monotonic counter the wisp bumps each time it accepts and applies a
  /// sealed wispOp. Sealed ops have no direct ACK; the app confirms one
  /// landed by watching this advance. Omitted at 0 (firmware default and
  /// legacy wisps); resets on wisp reboot.
  final int opSeq;

  /// Space-brightness factor (0..100) the wisp asserts on its claimed
  /// lamps: 100 = untouched, lower dims the whole space together. Absent on
  /// legacy wisps and omitted at the default; defaults to 100.
  final int brightness;

  /// Convenience: is either surface currently wisp-painted?
  bool get controlling => controllingBase || controllingShade;

  /// Source-mode: off / manual / aurora. Drives the top-of-pane pill
  /// picker. Parsed via `parseWispSourceMode`; defaults to `off` when
  /// the key is absent or unknown.
  final WispSourceMode source;

  /// Color the wisp renders on its own 30-pixel ring when sourceMode is
  /// Off. Does NOT propagate to the lamp grid. Off mode broadcasts
  /// RESTORE so the lamps drop any prior override. Defaults to a warm
  /// candle-amber matching the firmware fallback so pre-feature wisps
  /// (or `{}` payloads) land on a sensible value.
  final LampColor offColor;

  /// True when Aurora is viable: either `auroraConnected` is true or at
  /// least one zone has been observed. Either is sufficient evidence that
  /// there's a zone to follow. Selecting Aurora with no zones produces
  /// an empty palette.
  bool get auroraDetected =>
      auroraConnected || observedZones.isNotEmpty || currentZone != null;

  /// True iff the lamp has observed any wisp on the mesh. A wisp is
  /// "present" once either a hello or a status has arrived; the merged
  /// payload always carries a [wispMac] in that case.
  bool get present => wispMac != null;

  /// Decode the raw BLE characteristic bytes. Tolerates an empty
  /// payload, `"{}"`, and malformed JSON, all map to [empty]. Hello
  /// vs status fields are independent; missing keys default to the
  /// safest "unknown" value.
  factory WispStatus.fromBytes(Uint8List bytes) {
    if (bytes.isEmpty) return empty;
    final dynamic decoded;
    try {
      decoded = jsonDecode(utf8.decode(bytes));
    } catch (_) {
      return empty;
    }
    if (decoded is! Map<String, dynamic>) return empty;
    return WispStatus.fromJson(decoded);
  }

  factory WispStatus.fromJson(Map<String, dynamic> json) {
    int? asInt(Object? v) {
      if (v is int) return v;
      if (v is num) return v.toInt();
      if (v is String) return int.tryParse(v);
      return null;
    }

    bool asBool(Object? v) {
      if (v is bool) return v;
      if (v is num) return v != 0;
      return false;
    }

    String asString(Object? v) {
      if (v is String) return v;
      return '';
    }

    List<int> asIntList(Object? v) {
      if (v is List) {
        return [
          for (final item in v)
            if (asInt(item) != null) asInt(item)!,
        ];
      }
      return const <int>[];
    }

    LampColor parseOffColor(Object? v) {
      if (v is String) {
        try {
          return LampColor.fromHex(v);
        } catch (_) {
          return _defaultOffColor;
        }
      }
      if (v is List && v.length >= 3) {
        final r = asInt(v[0]);
        final g = asInt(v[1]);
        final b = asInt(v[2]);
        final w = v.length >= 4 ? (asInt(v[3]) ?? 0) : 0;
        if (r != null && g != null && b != null) {
          return LampColor(r: r & 0xFF, g: g & 0xFF, b: b & 0xFF, w: w & 0xFF);
        }
      }
      return _defaultOffColor;
    }

    LampColor? parseWispHexColor(Object? v) {
      if (v is! String) return null;
      try {
        return LampColor.fromHex(v);
      } catch (_) {
        return null;
      }
    }

    final zoneSrc = asString(json['zoneSource']);
    final sourceRaw = json['source'];
    return WispStatus(
      currentZone: asInt(json['currentZone']),
      zoneSource: parseZoneSource(zoneSrc.isEmpty ? null : zoneSrc),
      observedZones: asIntList(json['observedZones']),
      wifiConnected: asBool(json['wifiConnected']),
      wifiChannelMismatch: asBool(json['wifiChannelMismatch']),
      wifiApChannel: asInt(json['wifiApChannel']) ?? 0,
      auroraConnected: asBool(json['auroraConnected']),
      paletteIdPrefix: asString(json['paletteIdPrefix']),
      lastSeenMs: asInt(json['lastSeenMs']),
      wispMac: json['wispMac'] is String
          ? json['wispMac'] as String
          : null,
      wispVersion: asInt(json['wispVersion']),
      helloFlags: asInt(json['helloFlags']),
      helloPaletteIdPrefix: json['helloPaletteIdPrefix'] is String
          ? json['helloPaletteIdPrefix'] as String
          : null,
      helloLastSeenMs: asInt(json['helloLastSeenMs']),
      statusLastSeenMs: asInt(json['statusLastSeenMs']),
      // parseWispSourceMode tolerates null + unknown strings; defaults to off.
      source: parseWispSourceMode(sourceRaw is String ? sourceRaw : null),
      offColor: parseOffColor(json['offColor']),
      controllingBase: asBool(json['controllingBase']),
      controllingShade: asBool(json['controllingShade']),
      baseWispColor: parseWispHexColor(json['baseWispColor']),
      shadeWispColor: parseWispHexColor(json['shadeWispColor']),
      shuffleSeed: asInt(json['shuffleSeed']) ?? 0,
      driftIntervalMs: asInt(json['driftIntervalMs']) ?? 120000,
      driftFadePct: asInt(json['driftFadePct']) ?? 50,
      name: asString(json['name']),
      hasPassword: asBool(json['hasPassword']),
      ledType: json['ledType'] is String ? json['ledType'] as String : 'GRB',
      pixelCount: asInt(json['px']) ?? 30,
      opSeq: asInt(json['opSeq']) ?? 0,
      brightness: asInt(json['brightness']) ?? 100,
    );
  }

  WispStatus copyWith({
    int? currentZone,
    ZoneSource? zoneSource,
    List<int>? observedZones,
    WispSourceMode? source,
    LampColor? offColor,
    int? driftIntervalMs,
    int? driftFadePct,
    String? name,
    bool? hasPassword,
    String? ledType,
    int? pixelCount,
    int? brightness,
  }) {
    return WispStatus(
      currentZone: currentZone ?? this.currentZone,
      zoneSource: zoneSource ?? this.zoneSource,
      observedZones: observedZones ?? this.observedZones,
      wifiConnected: wifiConnected,
      wifiChannelMismatch: wifiChannelMismatch,
      wifiApChannel: wifiApChannel,
      auroraConnected: auroraConnected,
      paletteIdPrefix: paletteIdPrefix,
      lastSeenMs: lastSeenMs,
      wispMac: wispMac,
      wispVersion: wispVersion,
      helloFlags: helloFlags,
      helloPaletteIdPrefix: helloPaletteIdPrefix,
      helloLastSeenMs: helloLastSeenMs,
      statusLastSeenMs: statusLastSeenMs,
      source: source ?? this.source,
      offColor: offColor ?? this.offColor,
      controllingBase: controllingBase,
      controllingShade: controllingShade,
      baseWispColor: baseWispColor,
      shadeWispColor: shadeWispColor,
      shuffleSeed: shuffleSeed,
      driftIntervalMs: driftIntervalMs ?? this.driftIntervalMs,
      driftFadePct: driftFadePct ?? this.driftFadePct,
      name: name ?? this.name,
      hasPassword: hasPassword ?? this.hasPassword,
      ledType: ledType ?? this.ledType,
      pixelCount: pixelCount ?? this.pixelCount,
      opSeq: opSeq,
      brightness: brightness ?? this.brightness,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is WispStatus &&
          currentZone == other.currentZone &&
          zoneSource == other.zoneSource &&
          _observedZonesEq.equals(observedZones, other.observedZones) &&
          wifiConnected == other.wifiConnected &&
          wifiChannelMismatch == other.wifiChannelMismatch &&
          wifiApChannel == other.wifiApChannel &&
          auroraConnected == other.auroraConnected &&
          paletteIdPrefix == other.paletteIdPrefix &&
          lastSeenMs == other.lastSeenMs &&
          wispMac == other.wispMac &&
          wispVersion == other.wispVersion &&
          helloFlags == other.helloFlags &&
          helloPaletteIdPrefix == other.helloPaletteIdPrefix &&
          helloLastSeenMs == other.helloLastSeenMs &&
          statusLastSeenMs == other.statusLastSeenMs &&
          source == other.source &&
          offColor == other.offColor &&
          controllingBase == other.controllingBase &&
          controllingShade == other.controllingShade &&
          baseWispColor == other.baseWispColor &&
          shadeWispColor == other.shadeWispColor &&
          shuffleSeed == other.shuffleSeed &&
          driftIntervalMs == other.driftIntervalMs &&
          driftFadePct == other.driftFadePct &&
          name == other.name &&
          hasPassword == other.hasPassword &&
          ledType == other.ledType &&
          pixelCount == other.pixelCount &&
          opSeq == other.opSeq &&
          brightness == other.brightness;

  @override
  int get hashCode => Object.hash(
        currentZone,
        zoneSource,
        _observedZonesEq.hash(observedZones),
        wifiConnected,
        auroraConnected,
        paletteIdPrefix,
        lastSeenMs,
        wispMac,
        wispVersion,
        Object.hash(wifiChannelMismatch, wifiApChannel, helloFlags,
            helloPaletteIdPrefix, helloLastSeenMs),
        statusLastSeenMs,
        source,
        offColor,
        Object.hash(controllingBase, controllingShade, baseWispColor,
            shadeWispColor),
        shuffleSeed,
        driftIntervalMs,
        driftFadePct,
        Object.hash(
            name, hasPassword, ledType, pixelCount, opSeq, brightness),
      );
}
