#pragma once
#include <cstdint>
#include <optional>
#include <span>

#include "core/hw_config.hpp"

namespace lamp {

class Expression;
class FrameBuffer;

enum class ParamKind : uint8_t { Int, Enum };

struct Bound {
  enum Kind : uint8_t { Literal, Pixels } kind = Literal;
  int32_t v = 0;  // Pixels: cap (0 = uncapped); Literal: value.

  constexpr Bound() = default;
  // Implicit: .max=10 works in aggregate init of ParamSpec.
  constexpr Bound(int32_t n) : kind(Literal), v(n) {}

  static constexpr Bound pixels(int32_t cap = 0) { Bound b{}; b.kind = Pixels; b.v = cap; return b; }
};

struct EnumOption {
  int32_t value;
  const char* label;
  bool zoning = false;
  const char* group = nullptr;  // Motion family (Type) for the app's Type x Direction picker.
};

// Per-control option within an expression.
struct ParamSpec {
  const char* key;
  ParamKind kind;
  const char* label;
  int32_t min = 0;
  Bound max = {};
  int32_t step = 1;
  Bound def = {};
  const char* unit = nullptr;
  bool invert = false;
  const char* leftLabel = nullptr;
  const char* rightLabel = nullptr;
  const char* help = nullptr;
  bool requiresZoning = false;
  std::span<const EnumOption> options;
};

struct ColorSpec {
  uint8_t max = 0;
  const char* label = nullptr;
  const char* help = nullptr;
  bool inheritsSurface = false;
};

struct RangeSpec {
  int32_t min = 0;
  int32_t max = 0;
  int32_t step = 1;
  const char* unit = nullptr;
  int32_t defLo = 0;
  int32_t defHi = 0;
  // Minimum spread between the pair's lo and hi, same unit as min/max. 0 = no constraint.
  int32_t minGap = 0;
  const char* label = nullptr;
  const char* help = nullptr;
  // Param keys the fold writes defLo/defHi into (e.g. "intervalMin"/"intervalMax").
  const char* minKey = nullptr;
  const char* maxKey = nullptr;
};

inline constexpr const char* kIntervalHelp =
    "A random time in this range is picked before each trigger.";

// Shared "Motion" easing control. Values mirror util/easing.hpp Easing (0..17).
inline constexpr EnumOption kEasingOptions[] = {
  { .value = 0,  .label = "Linear",     .group = "Linear" },
  { .value = 4,  .label = "In",         .group = "Smooth" },
  { .value = 3,  .label = "Out",        .group = "Smooth" },
  { .value = 1,  .label = "In-out",     .group = "Smooth" },
  { .value = 5,  .label = "In",         .group = "Snap" },
  { .value = 6,  .label = "Out",        .group = "Snap" },
  { .value = 7,  .label = "In-out",     .group = "Snap" },
  { .value = 2,  .label = "Float",      .group = "Float"  },
  { .value = 8,  .label = "In",         .group = "Overshoot" },
  { .value = 9,  .label = "Out",        .group = "Overshoot" },
  { .value = 10, .label = "In-out",     .group = "Overshoot" },
  { .value = 11, .label = "In",         .group = "Spring" },
  { .value = 12, .label = "Out",        .group = "Spring" },
  { .value = 13, .label = "In-out",     .group = "Spring" },
  { .value = 14, .label = "In",         .group = "Bounce" },
  { .value = 15, .label = "Out",        .group = "Bounce" },
  { .value = 16, .label = "In-out",     .group = "Bounce" },
  { .value = 17, .label = "Random",     .group = "Random" },
};
inline constexpr const char* kEasingHelp =
    "Shape of the motion: Linear steady, Smooth eased ends, Snap fast then "
    "coast, Float lava drift, Overshoot blows past and eases back, Spring "
    "wobbles in, Bounce hops before it settles, Random rolls a new curve "
    "each fire.";
inline constexpr ParamSpec kEasingParam = {
  .key     = "easing",
  .kind    = ParamKind::Enum,
  .label   = "Motion",
  .max     = 17,
  .help    = kEasingHelp,
  .options = kEasingOptions,
};

// Continuity-of-travel key. Present on an expression's params -> its effective
// continuity follows the config value (Continuous == 1) rather than the
// descriptor's static continuous flag.
inline constexpr const char* kLoopParamKey = "loop";

// Shared "Opacity" cap. Min 10 (0 would be invisible; use disable for that).
inline constexpr const char* kOpacityHelp =
    "Caps how strong the effect gets at its peak. 100% is full colour.";
inline constexpr ParamSpec kOpacityParam = {
  .key   = "opacity",
  .kind  = ParamKind::Int,
  .label = "Opacity",
  .min   = 10,
  .max   = 100,
  .step  = 5,
  .def   = 100,
  .unit  = "%",
  .help  = kOpacityHelp,
};

// Top-level config for one expression. `params` are the per-control options above.
struct ExpressionDescriptor {
  const char* id;
  const char* name;
  bool continuous = false;
  ColorSpec colors;
  std::optional<RangeSpec> interval;
  std::optional<RangeSpec> duration;
  bool hasZone = false;
  bool zoneOptional = false;
  // Offer this expression only when the app's advanced mode is on. Additive
  // catalog field; absent means standard.
  bool advanced = false;
  std::span<const Surface> excludeTargets;
  std::span<const ParamSpec> params;
  // Plain fn-ptr keeps the descriptor a constexpr literal (no heap, constexpr-safe).
  Expression* (*make)(FrameBuffer*) = nullptr;
};

template <class T>
Expression* makeExpr(FrameBuffer* fb) { return new T(fb); }

// Binds .make onto make-less descriptor data. Each expression's header holds
// its descriptor as `inline constexpr` data without .make (the factory needs
// the complete class, which native tests can't link); the .cpp composes the
// registered descriptor with this.
constexpr ExpressionDescriptor withMake(ExpressionDescriptor d,
                                        Expression* (*make)(FrameBuffer*)) {
  d.make = make;
  return d;
}

}  // namespace lamp
