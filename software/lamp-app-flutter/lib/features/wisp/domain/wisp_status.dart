import 'dart:convert';
import 'dart:typed_data';

import '../../control/domain/lamp_color.dart';
import 'tuple_sampler.dart';
import 'wisp_config.dart';
import 'wisp_source_mode.dart';

/// Default Off-mode color. Matches the wisp firmware's NVS-default for
/// `offColor` (warm candle-amber); also the fallback when the status omits
/// it.
const LampColor _defaultOffColor = LampColor(r: 255, g: 150, b: 50, w: 0);

// Marks a nullable `_copy` argument left out, so null can replace a value.
const Object _keep = Object();

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
    this.wifiConnected = false,
    this.wifiChannelMismatch = false,
    this.wifiApChannel = 0,
    this.ethIp = '',
    this.sourceLive = false,
    this.paletteIdPrefix = '',
    this.lastSeenMs,
    this.wispMac,
    this.wispVersion,
    this.helloFlags,
    this.helloPaletteIdPrefix,
    this.helloLastSeenMs,
    this.statusLastSeenMs,
    this.source = WispSourceMode.off,
    this.oscRole = OscRole.primary,
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
    this.configKnown = false,
    this.configSplit = false,
    this.configLastSeenMs,
    this.manualId,
    this.manualSeq = 0,
    this.ip,
    this.paletteStyle,
    this.oscMode,
    this.oscSlot,
    this.heldConfigSeenMs,
  });

  /// Sentinel for "no wisp has been heard on this lamp yet" (lamp
  /// returned `"{}"`) and the parse-failure fallback.
  static const empty = WispStatus();

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

  /// The wisp's Ethernet address while its cable link holds a DHCP lease;
  /// empty otherwise. While set the wisp keeps WiFi off and takes the
  /// Straylight feed over the cable, so [wifiConnected] is false.
  final String ethIp;

  /// True while the wisp's palette stream is live: source is
  /// [WispSourceMode.osc] and a palette arrived recently. False in every
  /// other source mode.
  final bool sourceLive;

  /// First 8 hex chars of the palette ID the wisp last published.
  /// Empty string when the wisp hasn't published a palette yet.
  final String paletteIdPrefix;

  /// Milliseconds since the wisp booted, at the moment it serialised this
  /// status, on a clock that never wraps.
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
  /// Defaults to 0 matching the firmware default.
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
  /// Defaults to `'GRB'` matching the firmware default.
  final String ledType;

  /// Number of pixels in the wisp's LED ring. Defaults to 30 matching the
  /// firmware default.
  final int pixelCount;

  /// Monotonic counter the wisp bumps each time it accepts and applies a
  /// sealed wispOp. Sealed ops have no direct ACK; the app confirms one
  /// landed by watching this advance. 0 at the firmware default; resets on
  /// wisp reboot.
  final int opSeq;

  /// Space-brightness factor (0..100) the wisp asserts on its claimed
  /// lamps: 100 = untouched, lower dims the whole space together. Defaults
  /// to 100.
  final int brightness;

  /// True when [ledType], [pixelCount], [brightness], the drift fields,
  /// [shuffleSeed] and [opSeq] hold the wisp's real settings, which come
  /// only from a merged `wispConfig` ([withConfig]). False means those fields
  /// are placeholders a settings form must not show or write back.
  final bool configKnown;

  /// The wisp marked this status as carrying no settings; they come only
  /// from its `wispConfig`.
  final bool configSplit;

  /// Lamp-side `millis()` when the lamp cached the wisp's latest
  /// `wispConfig`. Absent on lamps that predate it, or until one arrives; a
  /// change means the `wispconfig` section holds something new.
  final int? configLastSeenMs;

  /// The wisp's stored Manual palette, as `WispRepository.statusManualId`
  /// computes it: `""` when the palette is empty, else its id, in every
  /// source mode. Null when the status does not carry it: a wisp that
  /// predates it, or the lamp's hello-only merge.
  final String? manualId;

  /// The `seq` of the palette save that stored the wisp's Manual palette,
  /// echoed until the next save; 0 when that save carried none, the wisp
  /// rebooted since, or the status does not carry it. Equality only.
  final int manualSeq;

  /// The wisp's OSC send-to address from its `wispConfig`: `""` while it has
  /// none, null until a config carrying it is merged (a wisp that predates
  /// it never sends one).
  final String? ip;

  /// How lamps take colours from the palette, from the wisp's `wispConfig`.
  /// Null until a config carrying it is merged: a wisp that predates it
  /// paints only gradients.
  final PaletteStyle? paletteStyle;

  /// Which OSC sender the wisp listens to, from its `wispConfig`. Null until
  /// a config carrying it is merged. A wisp that predates it follows only
  /// Straylight.
  final OscMode? oscMode;

  /// The Maestro palette slot the wisp reads, from its `wispConfig`. Null
  /// until a config carrying it is merged.
  final int? oscSlot;

  /// The lamp's `configLastSeenMs` stamp the merged config was read under,
  /// so the config's age can be told against [statusLastSeenMs]. Null until a
  /// config is merged, or when that read carried no stamp.
  final int? heldConfigSeenMs;

  /// The address an OSC sender should target, from the wisp's own reports:
  /// [ethIp] while the wisp is on Ethernet, since the wisp always picks it
  /// first, unless the merged config reached the lamp after this status;
  /// else [ip]. Empty when neither has one.
  String get oscSendTo =>
      ethIp.isNotEmpty && (ip == null || _statusAfterConfig)
          ? ethIp
          : (ip ?? '');

  // Whether the lamp cached this status no earlier than the merged config, on
  // the lamp's wrapping 32-bit millis. True when either stamp is absent.
  bool get _statusAfterConfig {
    final s = statusLastSeenMs;
    final c = heldConfigSeenMs;
    if (s == null || c == null) return true;
    return ((s - c) & 0xFFFFFFFF) < 0x80000000;
  }

  /// True when the wisp sent this status and its link state contradicts the
  /// held [ip]: on Ethernet with another address, a link up with no address,
  /// or an address with no link. A config frame lost after a link edge leaves
  /// them apart until the next config read. A status older than the merged
  /// config never disagrees: the config is the fresher report.
  bool get ipDisagreesWithLink {
    final held = ip;
    if (!fromWisp || !configKnown || held == null) return false;
    if (!_statusAfterConfig) return false;
    if (ethIp.isNotEmpty) return held != ethIp;
    final linked = wifiConnected;
    return linked == held.isEmpty;
  }

  /// True when the wisp sent this status. False for the lamp's hello-only
  /// merge, which it serves while it holds no status from the wisp.
  bool get fromWisp => configSplit;

  /// Convenience: is either surface currently wisp-painted?
  bool get controlling => controllingBase || controllingShade;

  /// Source-mode: off / manual / osc. Drives the top-of-pane pill
  /// picker. Parsed via `parseWispSourceMode`; defaults to `off` when
  /// the key is absent or unknown.
  final WispSourceMode source;

  /// Straylight palette role the wisp follows in [WispSourceMode.osc].
  /// Absent on the wire at the default, [OscRole.primary].
  final OscRole oscRole;

  /// Color the wisp renders on its own 30-pixel ring when sourceMode is
  /// Off. Does NOT propagate to the lamp grid. Off mode broadcasts
  /// RESTORE so the lamps drop any prior override. Defaults to a warm
  /// candle-amber matching the firmware fallback, since the status carries
  /// it only in Off mode.
  final LampColor offColor;

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

    LampColor parseOffColor(Object? v) {
      if (v is String) {
        try {
          return LampColor.fromHex(v);
        } catch (_) {
          return _defaultOffColor;
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

    final sourceRaw = json['source'];
    final oscRoleRaw = json['oscRole'];
    return WispStatus(
      wifiConnected: asBool(json['wifiConnected']),
      wifiChannelMismatch: asBool(json['wifiChannelMismatch']),
      wifiApChannel: asInt(json['wifiApChannel']) ?? 0,
      ethIp: asString(json['ethIp']),
      sourceLive: asBool(json['sourceLive']),
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
      oscRole: parseOscRole(oscRoleRaw is String ? oscRoleRaw : null),
      offColor: parseOffColor(json['offColor']),
      controllingBase: asBool(json['controllingBase']),
      controllingShade: asBool(json['controllingShade']),
      baseWispColor: parseWispHexColor(json['baseWispColor']),
      shadeWispColor: parseWispHexColor(json['shadeWispColor']),
      name: asString(json['name']),
      hasPassword: asBool(json['hasPassword']),
      configSplit: json['configSplit'] == true,
      configLastSeenMs: asInt(json['configLastSeenMs']),
      manualId: json['manualId'] is String ? json['manualId'] as String : null,
      manualSeq: asInt(json['manualSeq']) ?? 0,
    );
  }

  /// This status with [config]'s settings, marked [configKnown]. [seenMs] is
  /// the lamp stamp the config was read under ([heldConfigSeenMs]).
  WispStatus withConfig(WispConfig config, {int? seenMs}) {
    return _copy(
      ledType: config.ledType,
      pixelCount: config.pixelCount,
      brightness: config.brightness,
      driftIntervalMs: config.driftIntervalMs,
      driftFadePct: config.driftFadePct,
      shuffleSeed: config.shuffleSeed,
      opSeq: config.opSeq,
      configKnown: true,
      ip: config.ip,
      paletteStyle: config.paletteStyle,
      oscMode: config.oscMode,
      oscSlot: config.oscSlot,
      heldConfigSeenMs: seenMs,
    );
  }

  WispStatus copyWith({
    WispSourceMode? source,
    OscRole? oscRole,
    LampColor? offColor,
    int? driftIntervalMs,
    int? driftFadePct,
    String? name,
    bool? hasPassword,
    String? ledType,
    int? pixelCount,
    int? brightness,
    PaletteStyle? paletteStyle,
    OscMode? oscMode,
    int? oscSlot,
  }) {
    return _copy(
      source: source,
      oscRole: oscRole,
      offColor: offColor,
      driftIntervalMs: driftIntervalMs,
      driftFadePct: driftFadePct,
      name: name,
      hasPassword: hasPassword,
      ledType: ledType,
      pixelCount: pixelCount,
      brightness: brightness,
      paletteStyle: paletteStyle ?? _keep,
      oscMode: oscMode ?? _keep,
      oscSlot: oscSlot ?? _keep,
    );
  }

  WispStatus _copy({
    WispSourceMode? source,
    OscRole? oscRole,
    LampColor? offColor,
    int? driftIntervalMs,
    int? driftFadePct,
    String? name,
    bool? hasPassword,
    String? ledType,
    int? pixelCount,
    int? brightness,
    int? shuffleSeed,
    int? opSeq,
    bool? configKnown,
    Object? ip = _keep,
    Object? paletteStyle = _keep,
    Object? oscMode = _keep,
    Object? oscSlot = _keep,
    Object? heldConfigSeenMs = _keep,
  }) {
    return WispStatus(
      wifiConnected: wifiConnected,
      wifiChannelMismatch: wifiChannelMismatch,
      wifiApChannel: wifiApChannel,
      ethIp: ethIp,
      sourceLive: sourceLive,
      paletteIdPrefix: paletteIdPrefix,
      lastSeenMs: lastSeenMs,
      wispMac: wispMac,
      wispVersion: wispVersion,
      helloFlags: helloFlags,
      helloPaletteIdPrefix: helloPaletteIdPrefix,
      helloLastSeenMs: helloLastSeenMs,
      statusLastSeenMs: statusLastSeenMs,
      source: source ?? this.source,
      oscRole: oscRole ?? this.oscRole,
      offColor: offColor ?? this.offColor,
      controllingBase: controllingBase,
      controllingShade: controllingShade,
      baseWispColor: baseWispColor,
      shadeWispColor: shadeWispColor,
      shuffleSeed: shuffleSeed ?? this.shuffleSeed,
      driftIntervalMs: driftIntervalMs ?? this.driftIntervalMs,
      driftFadePct: driftFadePct ?? this.driftFadePct,
      name: name ?? this.name,
      hasPassword: hasPassword ?? this.hasPassword,
      ledType: ledType ?? this.ledType,
      pixelCount: pixelCount ?? this.pixelCount,
      opSeq: opSeq ?? this.opSeq,
      brightness: brightness ?? this.brightness,
      configKnown: configKnown ?? this.configKnown,
      configSplit: configSplit,
      configLastSeenMs: configLastSeenMs,
      manualId: manualId,
      manualSeq: manualSeq,
      ip: identical(ip, _keep) ? this.ip : ip as String?,
      paletteStyle: identical(paletteStyle, _keep)
          ? this.paletteStyle
          : paletteStyle as PaletteStyle?,
      oscMode: identical(oscMode, _keep) ? this.oscMode : oscMode as OscMode?,
      oscSlot: identical(oscSlot, _keep) ? this.oscSlot : oscSlot as int?,
      heldConfigSeenMs: identical(heldConfigSeenMs, _keep)
          ? this.heldConfigSeenMs
          : heldConfigSeenMs as int?,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is WispStatus &&
          wifiConnected == other.wifiConnected &&
          wifiChannelMismatch == other.wifiChannelMismatch &&
          wifiApChannel == other.wifiApChannel &&
          ethIp == other.ethIp &&
          sourceLive == other.sourceLive &&
          paletteIdPrefix == other.paletteIdPrefix &&
          lastSeenMs == other.lastSeenMs &&
          wispMac == other.wispMac &&
          wispVersion == other.wispVersion &&
          helloFlags == other.helloFlags &&
          helloPaletteIdPrefix == other.helloPaletteIdPrefix &&
          helloLastSeenMs == other.helloLastSeenMs &&
          statusLastSeenMs == other.statusLastSeenMs &&
          source == other.source &&
          oscRole == other.oscRole &&
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
          brightness == other.brightness &&
          configKnown == other.configKnown &&
          configSplit == other.configSplit &&
          configLastSeenMs == other.configLastSeenMs &&
          manualId == other.manualId &&
          manualSeq == other.manualSeq &&
          ip == other.ip &&
          paletteStyle == other.paletteStyle &&
          oscMode == other.oscMode &&
          oscSlot == other.oscSlot &&
          heldConfigSeenMs == other.heldConfigSeenMs;

  @override
  int get hashCode => Object.hash(
        wifiConnected,
        sourceLive,
        paletteIdPrefix,
        lastSeenMs,
        wispMac,
        wispVersion,
        Object.hash(wifiChannelMismatch, wifiApChannel, ethIp, helloFlags,
            helloPaletteIdPrefix, helloLastSeenMs),
        statusLastSeenMs,
        Object.hash(source, oscRole),
        offColor,
        Object.hash(controllingBase, controllingShade, baseWispColor,
            shadeWispColor),
        shuffleSeed,
        driftIntervalMs,
        driftFadePct,
        Object.hash(
          name,
          hasPassword,
          ledType,
          pixelCount,
          opSeq,
          brightness,
          configKnown,
          Object.hash(configSplit, configLastSeenMs, manualId, manualSeq, ip,
              paletteStyle, oscMode, oscSlot, heldConfigSeenMs),
        ),
      );
}
