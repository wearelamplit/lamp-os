import 'package:collection/collection.dart';

import '../../social/domain/social_mode.dart';
import 'lamp_color.dart';

const _listEq = ListEquality<Object?>();
const _mapEq = MapEquality<Object?, Object?>();

/// CHAR_LAMP_SECTION payload, see firmware Config::asLampJson.
///
/// Manually-overridden `==` / `hashCode`: Riverpod's
/// `.select` and AsyncValue equality short-circuit on `prev == next`.
/// Without explicit equality the default identity compare always misses
/// — every notifier rebuild propagated to every consumer even when no
/// observable field changed. Full @freezed conversion was rejected
/// because the `fromJson` factories carry non-trivial coercion logic
/// (knockout list → map, byteOrder fallback from bpp, etc.) that the
/// generator can't faithfully reproduce.
class LampSection {
  const LampSection({
    required this.name,
    required this.brightness,
    required this.advancedEnabled,
    required this.webappEnabled,
    required this.socialMode,
    this.devMode = false,
    this.fwVersion,
    this.fwChannel,
    this.hasPassword,
    this.lampType,
  });

  final String name;
  final int brightness;
  final bool advancedEnabled;
  // Default true on missing — older firmware without the field still ran
  // the on-device webapp at boot, so the absent payload IS "enabled".
  final bool webappEnabled;
  final SocialMode socialMode;

  /// Lamp-persisted "always treat as advanced" flag. Independent of the
  /// session-only advancedSession unlock. Also gates the cascade-to-other-
  /// lamps controls in the expression editor, which are NOT shown in
  /// regular advanced mode.
  final bool devMode;

  /// Firmware semver, packed as `(major << 16) | (minor << 8) | patch`.
  /// Nullable for backward compat with older firmware that doesn't yet
  /// emit this field (the Info tab renders "..." in that case).
  final int? fwVersion;

  /// Firmware release channel: `'dev' | 'beta' | 'stable'`. Nullable for
  /// the same backward-compat reason as `fwVersion`.
  final String? fwChannel;

  /// Whether the lamp's NVS has a non-empty controlPassword set. Lets
  /// the app detect divergence between its cached pw and the lamp's
  /// actual state — when the lamp was reflashed/NVS-wiped, this comes
  /// back false and the app clears its cached pw so settings_blob
  /// writes fall back to plaintext. Null on older firmware that doesn't
  /// emit the field — the divergence heal is opt-in on its presence.
  final bool? hasPassword;

  /// Lamp variant identity — `'standard'`, `'snafu'`, etc. Firmware-owned
  /// (resolved at first boot via the LAMP_INITIAL_TYPE build flag, then
  /// persisted in NVS). Used by the app to fetch the matching per-variant
  /// firmware binary at OTA time. Nullable for backward compat with older
  /// firmware that doesn't yet emit the field.
  final String? lampType;

  factory LampSection.fromJson(Map<String, dynamic> json) => LampSection(
        name: (json['name'] as String?) ?? '',
        brightness: (json['brightness'] as num?)?.toInt() ?? 100,
        advancedEnabled: json['advancedEnabled'] as bool? ?? false,
        webappEnabled: json['webappEnabled'] as bool? ?? true,
        socialMode:
            SocialMode.fromWire((json['socialMode'] as num?)?.toInt()),
        devMode: json['devMode'] as bool? ?? false,
        fwVersion: (json['fwVersion'] as num?)?.toInt(),
        fwChannel: json['fwChannel'] as String?,
        hasPassword: json['hasPassword'] as bool?,
        lampType: json['lampType'] as String?,
      );

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is LampSection &&
          name == other.name &&
          brightness == other.brightness &&
          advancedEnabled == other.advancedEnabled &&
          webappEnabled == other.webappEnabled &&
          socialMode == other.socialMode &&
          devMode == other.devMode &&
          fwVersion == other.fwVersion &&
          fwChannel == other.fwChannel &&
          hasPassword == other.hasPassword &&
          lampType == other.lampType;

  @override
  int get hashCode => Object.hash(
        name,
        brightness,
        advancedEnabled,
        webappEnabled,
        socialMode,
        devMode,
        fwVersion,
        fwChannel,
        hasPassword,
        lampType,
      );
}

/// CHAR_BASE_SECTION payload, see firmware Config::asBaseJson.
class BaseSection {
  const BaseSection({
    required this.px,
    required this.ac,
    required this.bpp,
    required this.byteOrder,
    required this.colors,
    required this.knockout,
    this.colorsEditable = true,
  });

  final int px;
  final int ac;
  final int bpp;

  /// NeoPixel wire byte order: `GRBW` (4 bpp), `GRB` (3 bpp), or `BGR`
  /// (3 bpp). Source of truth for strip type; `bpp` is kept in sync so
  /// older firmware that hasn't grown the `byteOrder` field still
  /// behaves correctly via the bpp-derived NeoPixel default.
  final String byteOrder;

  final List<LampColor> colors;

  /// Per-LED brightness overrides (0..100). Indices absent from the map use
  /// the default 100 %. Empty map = all LEDs at full brightness. Stored as a
  /// `Map<int, int>` rather than a list for O(1) lookup and small memory.
  final Map<int, int> knockout;

  /// Whether the app should expose the color picker for this surface.
  /// Firmware-owned — set by the Lamp subclass via applyDefaults; defaults
  /// to true for backward compat with older firmware that doesn't emit it.
  final bool colorsEditable;

  factory BaseSection.fromJson(Map<String, dynamic> json) {
    // Knockout is a positional int array: index = pixel, value = brightness
    // % (0..100, default 100). Only non-default entries are kept in the
    // map (firmware's `asBaseJson` emits a full-length array, but the app
    // only cares about overrides).
    final knockoutList = (json['knockout'] as List?) ?? const [];
    final knockoutMap = <int, int>{};
    for (var i = 0; i < knockoutList.length; i++) {
      final raw = knockoutList[i];
      if (raw is num) {
        final b = raw.toInt();
        if (b != 100) knockoutMap[i] = b;
      }
    }
    final bpp = (json['bpp'] as num?)?.toInt() ?? 4;
    // `byteOrder` lands as a string on the wire when the firmware
    // supports it. When absent (older firmware), derive from `bpp`.
    final byteOrder = (json['byteOrder'] as String?)?.trim().isNotEmpty == true
        ? json['byteOrder'] as String
        : (bpp == 4 ? 'GRBW' : 'GRB');
    return BaseSection(
      px: (json['px'] as num?)?.toInt() ?? 35,
      ac: (json['ac'] as num?)?.toInt() ?? 0,
      bpp: bpp,
      byteOrder: byteOrder,
      colors: ((json['colors'] as List?) ?? const [])
          .map((e) => LampColor.fromHex(e as String))
          .toList(),
      knockout: knockoutMap,
      colorsEditable: (json['colorsEditable'] as bool?) ?? true,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is BaseSection &&
          px == other.px &&
          ac == other.ac &&
          bpp == other.bpp &&
          byteOrder == other.byteOrder &&
          _listEq.equals(colors, other.colors) &&
          _mapEq.equals(knockout, other.knockout) &&
          colorsEditable == other.colorsEditable;

  @override
  int get hashCode => Object.hash(
        px,
        ac,
        bpp,
        byteOrder,
        _listEq.hash(colors),
        _mapEq.hash(knockout),
        colorsEditable,
      );
}

/// CHAR_SHADE_SECTION payload, see firmware Config::asShadeJson.
class ShadeSection {
  const ShadeSection({
    required this.px,
    required this.bpp,
    required this.byteOrder,
    required this.colors,
    this.colorsEditable = true,
  });

  final int px;
  final int bpp;

  /// NeoPixel wire byte order; see BaseSection.byteOrder.
  final String byteOrder;

  final List<LampColor> colors;

  /// Whether the app should expose the color picker for this surface.
  /// Firmware-owned — set by the Lamp subclass via applyDefaults; defaults
  /// to true for backward compat with older firmware that doesn't emit it.
  final bool colorsEditable;

  factory ShadeSection.fromJson(Map<String, dynamic> json) {
    final bpp = (json['bpp'] as num?)?.toInt() ?? 4;
    final byteOrder = (json['byteOrder'] as String?)?.trim().isNotEmpty == true
        ? json['byteOrder'] as String
        : (bpp == 4 ? 'GRBW' : 'GRB');
    return ShadeSection(
      px: (json['px'] as num?)?.toInt() ?? 38,
      bpp: bpp,
      byteOrder: byteOrder,
      colors: ((json['colors'] as List?) ?? const [])
          .map((e) => LampColor.fromHex(e as String))
          .toList(),
      colorsEditable: (json['colorsEditable'] as bool?) ?? true,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is ShadeSection &&
          px == other.px &&
          bpp == other.bpp &&
          byteOrder == other.byteOrder &&
          _listEq.equals(colors, other.colors) &&
          colorsEditable == other.colorsEditable;

  @override
  int get hashCode =>
      Object.hash(px, bpp, byteOrder, _listEq.hash(colors), colorsEditable);
}

/// CHAR_HOME_SECTION payload. Presence-only home mode: the lamp never
/// stores a password (no association — just SSID-visibility detection),
/// so this section carries only the SSID, brightness, and the soft
/// on/off toggle.
class HomeSection {
  const HomeSection({
    required this.ssid,
    required this.brightness,
    required this.enabled,
  });

  final String ssid;
  final int brightness;

  /// Soft on/off for Home Mode. When false, the lamp ignores SSID
  /// visibility and stays in regular mode.
  final bool enabled;

  factory HomeSection.fromJson(Map<String, dynamic> json) {
    final ssid = (json['ssid'] as String?) ?? '';
    return HomeSection(
      ssid: ssid,
      brightness: (json['brightness'] as num?)?.toInt() ?? 60,
      enabled: (json['enabled'] as bool?) ?? ssid.isNotEmpty,
    );
  }

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is HomeSection &&
          ssid == other.ssid &&
          brightness == other.brightness &&
          enabled == other.enabled;

  @override
  int get hashCode => Object.hash(ssid, brightness, enabled);
}

/// A single expression configuration. CHAR_EXPRESSION_SECTION returns an array
/// of these; see firmware ExpressionConfig.
class ExpressionConfig {
  const ExpressionConfig({
    required this.type,
    required this.enabled,
    required this.colors,
    required this.intervalMin,
    required this.intervalMax,
    required this.target,
    required this.parameters,
  });

  final String type;
  final bool enabled;
  final List<LampColor> colors;
  final int intervalMin;
  final int intervalMax;
  final int target; // 1=shade, 2=base, 3=both
  final Map<String, int> parameters;

  // (Refactor 2026-06-13: `disabledDuringWispOverride` field removed.
  // It's a pure type-property now — look up via
  // `ExpressionTypeMeta.forType(type).defaultDisabledDuringWispOverride`
  // when the UI needs to gate against wisp control. Mirrors the firmware
  // refactor that moved the gate to a virtual method on the Expression
  // subclass.)

  static const _reservedKeys = {
    'type', 'enabled', 'colors', 'intervalMin', 'intervalMax', 'target',
    // Tolerated from older firmware payloads but dropped from params.
    'disabledDuringWispOverride',
  };

  factory ExpressionConfig.fromJson(Map<String, dynamic> json) {
    final params = <String, int>{};
    for (final e in json.entries) {
      if (_reservedKeys.contains(e.key)) continue;
      if (e.value is num) params[e.key] = (e.value as num).toInt();
    }
    return ExpressionConfig(
      type: json['type'] as String? ?? '',
      enabled: json['enabled'] as bool? ?? false,
      colors: ((json['colors'] as List?) ?? const [])
          .map((e) => LampColor.fromHex(e as String))
          .toList(),
      intervalMin: (json['intervalMin'] as num?)?.toInt() ?? 60,
      intervalMax: (json['intervalMax'] as num?)?.toInt() ?? 900,
      target: (json['target'] as num?)?.toInt() ?? 3,
      parameters: params,
    );
  }

  Map<String, dynamic> toJson() => {
        'type': type,
        'enabled': enabled,
        'colors': colors.map((c) => c.toHex()).toList(),
        'intervalMin': intervalMin,
        'intervalMax': intervalMax,
        'target': target,
        ...parameters,
      };

  ExpressionConfig copyWith({
    String? type,
    bool? enabled,
    List<LampColor>? colors,
    int? intervalMin,
    int? intervalMax,
    int? target,
    Map<String, int>? parameters,
  }) =>
      ExpressionConfig(
        type: type ?? this.type,
        enabled: enabled ?? this.enabled,
        colors: colors ?? this.colors,
        intervalMin: intervalMin ?? this.intervalMin,
        intervalMax: intervalMax ?? this.intervalMax,
        target: target ?? this.target,
        parameters: parameters ?? this.parameters,
      );

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is ExpressionConfig &&
          type == other.type &&
          enabled == other.enabled &&
          _listEq.equals(colors, other.colors) &&
          intervalMin == other.intervalMin &&
          intervalMax == other.intervalMax &&
          target == other.target &&
          _mapEq.equals(parameters, other.parameters);

  @override
  int get hashCode => Object.hash(
        type,
        enabled,
        _listEq.hash(colors),
        intervalMin,
        intervalMax,
        target,
        _mapEq.hash(parameters),
      );
}

/// CHAR_EXPRESSION_SECTION payload — a JSON array of ExpressionConfig objects.
class ExpressionsSection {
  const ExpressionsSection({required this.expressions});

  final List<ExpressionConfig> expressions;

  factory ExpressionsSection.fromJson(List<dynamic> json) =>
      ExpressionsSection(
        expressions: json
            .cast<Map<String, dynamic>>()
            .map(ExpressionConfig.fromJson)
            .toList(),
      );

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is ExpressionsSection &&
          _listEq.equals(expressions, other.expressions);

  @override
  int get hashCode => _listEq.hash(expressions);
}
