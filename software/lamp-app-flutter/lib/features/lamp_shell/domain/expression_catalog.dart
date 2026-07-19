import 'package:collection/collection.dart';

import '../../control/domain/sections.dart';

/// Parsed `exprcat` page-section: the firmware-declared expression catalog.
/// The firmware is the single source of truth for what parameters each
/// expression accepts, their ranges, and their types; the app renders the
/// editor generically from this. Wire format documented in
/// `docs/dev/expressions.md`.

/// A structured bound. Either a literal value or a value resolved against the
/// target surface's pixel count (optionally capped). Wire form: a plain
/// number (literal) or `{"rel":"pixels","cap":N?}`.
class Bound {
  const Bound.literal(this.value)
      : cap = null,
        isPixels = false;
  const Bound.pixels({this.cap})
      : value = 0,
        isPixels = true;

  final int value;
  final int? cap;
  final bool isPixels;

  int resolve(int pixelCount) {
    if (!isPixels) return value;
    final c = cap;
    if (c != null && c > 0) return pixelCount < c ? pixelCount : c;
    return pixelCount;
  }

  factory Bound.fromJson(Object? json) {
    if (json is num) return Bound.literal(json.toInt());
    if (json is Map) return Bound.pixels(cap: (json['cap'] as num?)?.toInt());
    return const Bound.literal(0);
  }
}

enum ParamType { integer, enumeration }

/// Firmware param keys the app treats specially (mirrors
/// `software/lamp-os/src/expressions/expression_schema.hpp`). The `easing`
/// param renders as the Motion picker; the `loop` param drives
/// [effectiveContinuous].
const String kEasingParamKey = 'easing';
const String kLoopParamKey = 'loop';

/// Effective continuity for grouping, the per-row play button, and params
/// gating. Mirrors the firmware: a `loop` param set on the config wins over
/// the descriptor's static [ExpressionDescriptor.continuous] flag (any
/// non-zero value == continuous, matching `getParam(..., kLoopParamKey) != 0`).
bool effectiveContinuous(
  ExpressionDescriptor? descriptor,
  Map<String, int> parameters,
) {
  final loop = parameters[kLoopParamKey];
  if (loop != null) return loop != 0;
  return descriptor?.continuous ?? false;
}

/// Cooldown for a triggerable expression that has no duration range (e.g.
/// pulse in Trigger mode). Tunable.
const int kTriggerCooldownFallbackMs = 2000;

/// How long a ▶ trigger stays disabled after firing one cycle, so the
/// transient plays out before it can be re-fired. A duration-bearing
/// expression uses its configured durationMax (falling back to the
/// descriptor's default), converted to ms via the range's unit; a
/// duration-less one uses [kTriggerCooldownFallbackMs].
Duration triggerCooldown(
  ExpressionDescriptor? descriptor,
  ExpressionConfig config,
) {
  final duration = descriptor?.duration;
  if (duration == null) {
    return const Duration(milliseconds: kTriggerCooldownFallbackMs);
  }
  final key = duration.maxKey;
  final durationMax =
      (key != null ? config.parameters[key] : null) ?? duration.defHi;
  final factor = duration.unit == 's' ? 1000 : 1;
  return Duration(milliseconds: durationMax * factor);
}

class EnumOption {
  const EnumOption({
    required this.value,
    required this.label,
    this.zoning = false,
  });

  final int value;
  final String label;

  /// Selecting this option activates the zoning state, revealing the zone
  /// control and any `requiresZoning` params.
  final bool zoning;

  factory EnumOption.fromJson(Map<String, dynamic> j) => EnumOption(
        value: (j['value'] as num).toInt(),
        label: j['label'] as String,
        zoning: j['zoning'] as bool? ?? false,
      );
}

class CatalogParam {
  const CatalogParam({
    required this.key,
    required this.type,
    required this.label,
    required this.min,
    required this.max,
    required this.step,
    required this.def,
    this.unit,
    this.invert = false,
    this.leftLabel,
    this.rightLabel,
    this.help,
    this.requiresZoning = false,
    this.options = const [],
  });

  final String key;
  final ParamType type;
  final String label;
  final int min;
  final Bound max;
  final int step;
  final Bound def;
  final String? unit;
  final bool invert;
  final String? leftLabel;
  final String? rightLabel;
  final String? help;
  final bool requiresZoning;
  final List<EnumOption> options;

  factory CatalogParam.fromJson(Map<String, dynamic> j) => CatalogParam(
        key: j['key'] as String,
        type: j['type'] == 'enum' ? ParamType.enumeration : ParamType.integer,
        label: j['label'] as String? ?? '',
        min: (j['min'] as num?)?.toInt() ?? 0,
        max: Bound.fromJson(j['max']),
        step: (j['step'] as num?)?.toInt() ?? 1,
        def: Bound.fromJson(j['default']),
        unit: j['unit'] as String?,
        invert: j['invert'] as bool? ?? false,
        leftLabel: j['leftLabel'] as String?,
        rightLabel: j['rightLabel'] as String?,
        help: j['help'] as String?,
        requiresZoning: j['requiresZoning'] as bool? ?? false,
        options: ((j['options'] as List?) ?? const [])
            .map((e) => EnumOption.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}

/// A two-thumbed range control. `interval` writes to the instance's top-level
/// `intervalMin`/`intervalMax`; `duration` writes to the params map under
/// [minKey]/[maxKey].
class CatalogRange {
  const CatalogRange({
    required this.min,
    required this.max,
    required this.step,
    required this.defLo,
    required this.defHi,
    this.unit,
    this.label,
    this.help,
    this.minKey,
    this.maxKey,
  });

  final int min;
  final int max;
  final int step;
  final int defLo;
  final int defHi;
  final String? unit;
  final String? label;
  final String? help;
  final String? minKey;
  final String? maxKey;

  factory CatalogRange.fromJson(Map<String, dynamic> j) {
    final def = (j['default'] as List?) ?? const [];
    return CatalogRange(
      min: (j['min'] as num?)?.toInt() ?? 0,
      max: (j['max'] as num?)?.toInt() ?? 0,
      step: (j['step'] as num?)?.toInt() ?? 1,
      defLo: def.isNotEmpty ? (def[0] as num).toInt() : 0,
      defHi: def.length > 1 ? (def[1] as num).toInt() : 0,
      unit: j['unit'] as String?,
      label: j['label'] as String?,
      help: j['help'] as String?,
      minKey: j['minKey'] as String?,
      maxKey: j['maxKey'] as String?,
    );
  }
}

class CatalogColors {
  const CatalogColors({
    required this.max,
    this.label,
    this.help,
    this.inheritsSurface = false,
  });

  final int max;
  final String? label;
  final String? help;

  /// An empty palette is valid and means "follow the surface's own colors".
  final bool inheritsSurface;

  factory CatalogColors.fromJson(Map<String, dynamic>? j) {
    if (j == null) return const CatalogColors(max: 0);
    return CatalogColors(
      max: (j['max'] as num?)?.toInt() ?? 0,
      label: j['label'] as String?,
      help: j['help'] as String?,
      inheritsSurface: j['inheritsSurface'] as bool? ?? false,
    );
  }
}

class ExpressionDescriptor {
  const ExpressionDescriptor({
    required this.id,
    required this.name,
    this.continuous = false,
    this.pausesWispOverride = false,
    required this.colors,
    this.interval,
    this.duration,
    this.hasZone = false,
    this.zoneOptional = false,
    this.excludeTargets = const [],
    this.params = const [],
  });

  final String id;
  final String name;
  final bool continuous;

  /// Greys/pauses the expression while a wisp is overriding colors.
  final bool pausesWispOverride;
  final CatalogColors colors;
  final CatalogRange? interval;
  final CatalogRange? duration;
  final bool hasZone;

  /// When true the zone is opt-in via a whole-strip/region toggle; when false
  /// (with [hasZone]) the zone is always available unless a zoning enum gates it.
  final bool zoneOptional;

  /// Surface strings this expression cannot target.
  final List<String> excludeTargets;
  final List<CatalogParam> params;

  factory ExpressionDescriptor.fromJson(Map<String, dynamic> j) {
    final zone = j['zone'];
    return ExpressionDescriptor(
      id: j['id'] as String,
      name: j['name'] as String? ?? '',
      continuous: j['continuous'] as bool? ?? false,
      pausesWispOverride: j['pausesWispOverride'] as bool? ?? false,
      colors: CatalogColors.fromJson(j['colors'] as Map<String, dynamic>?),
      interval: j['interval'] == null
          ? null
          : CatalogRange.fromJson(j['interval'] as Map<String, dynamic>),
      duration: j['duration'] == null
          ? null
          : CatalogRange.fromJson(j['duration'] as Map<String, dynamic>),
      hasZone: zone != null,
      zoneOptional: zone is Map && (zone['optional'] as bool? ?? false),
      excludeTargets:
          ((j['excludeTargets'] as List?) ?? const []).cast<String>(),
      params: ((j['params'] as List?) ?? const [])
          .map((e) => CatalogParam.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }
}

class ExpressionCatalog {
  const ExpressionCatalog({
    required this.schemaVersion,
    required this.expressions,
  });

  final int schemaVersion;
  final List<ExpressionDescriptor> expressions;

  ExpressionDescriptor? byId(String id) =>
      expressions.firstWhereOrNull((e) => e.id == id);

  factory ExpressionCatalog.fromJson(Map<String, dynamic> j) =>
      ExpressionCatalog(
        schemaVersion: (j['schemaVersion'] as num?)?.toInt() ?? 0,
        expressions: ((j['expressions'] as List?) ?? const [])
            .map((e) =>
                ExpressionDescriptor.fromJson(e as Map<String, dynamic>))
            .toList(),
      );
}
