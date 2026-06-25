// software/lamp-os/src/core/lamp_variants.cpp
//
// The variant registry — single source of truth for "what lamps does
// this binary support". Per-variant build: exactly one
// LAMP_BUILD_VARIANT_<TYPE> must be defined (injected via the matching
// [env:upesy_wroom_<type>] block in platformio.ini). The unused
// variant's source directory is excluded from compile by build_src_filter,
// so neither the include below nor the array entry compiles for the
// other variant.
//
// Adding a new variant requires updating four sites in lockstep:
//   1. the `[env:upesy_wroom_<type>]` block in platformio.ini
//   2. the conditional `#include` below
//   3. the conditional `#elif defined(...)` factory arm
//   4. the `#if (defined(...) + ...) != 1` invariant

#include "core/lamp_variants.hpp"
#include "core/lamp.hpp"

#ifdef LAMP_BUILD_VARIANT_STANDARD
#include "lamps/standard/standard_lamp.hpp"
#endif
#ifdef LAMP_BUILD_VARIANT_SNAFU
#include "lamps/snafu/snafu_lamp.hpp"
#endif

#if (defined(LAMP_BUILD_VARIANT_STANDARD) + defined(LAMP_BUILD_VARIANT_SNAFU)) != 1
#error "Exactly one LAMP_BUILD_VARIANT_* must be defined (check platformio.ini env)"
#endif

#include <array>
#include <cstdio>
#include <string_view>

namespace lamp {

namespace {

struct NamedFactory {
  std::string_view name;
  std::unique_ptr<Lamp> (*make)();
};

#ifdef LAMP_BUILD_VARIANT_STANDARD
inline std::unique_ptr<Lamp> makeStandard() {
  return std::make_unique<StandardLamp>();
}
#endif
#ifdef LAMP_BUILD_VARIANT_SNAFU
inline std::unique_ptr<Lamp> makeSnafu() {
  return std::make_unique<SnafuLamp>();
}
#endif

constexpr std::array<NamedFactory, 1> kLampVariants = {{
#ifdef LAMP_BUILD_VARIANT_STANDARD
  {"standard", makeStandard},
#elif defined(LAMP_BUILD_VARIANT_SNAFU)
  {"snafu", makeSnafu},
#endif
}};

}  // namespace

std::unique_ptr<Lamp> createLampByType(const std::string& type) {
  for (const auto& v : kLampVariants) {
    if (type == v.name) return v.make();
  }
  return nullptr;
}

const char* knownLampTypes() {
  // Lazy-built once; safe — single-threaded init phase. Long enough
  // for ~10 short variant names.
  static char buf[128] = {0};
  if (buf[0]) return buf;
  size_t off = 0;
  for (const auto& v : kLampVariants) {
    int n = std::snprintf(buf + off, sizeof(buf) - off, "%s%.*s",
                          off ? ", " : "",
                          static_cast<int>(v.name.size()), v.name.data());
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf) - off) break;
    off += static_cast<size_t>(n);
  }
  return buf;
}

}  // namespace lamp
