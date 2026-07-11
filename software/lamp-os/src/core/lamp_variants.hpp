// software/lamp-os/src/core/lamp_variants.hpp
//
// Compile-time variant identity. Each per-variant binary compiles in exactly
// one LAMP_BUILD_VARIANT_* (set by the [env:upesy_wroom_<type>] block in
// platformio.ini). createCompiledLamp() and compiledLampType() return the
// compiled-in instance/name directly. No runtime registry lookup.

#pragma once
#include <memory>

#ifdef LAMP_BUILD_VARIANT_STANDARD
#include "lamps/standard/standard_lamp.hpp"
#endif
#ifdef LAMP_BUILD_VARIANT_SNAFU
#include "lamps/snafu/snafu_lamp.hpp"
#endif

#if (defined(LAMP_BUILD_VARIANT_STANDARD) + defined(LAMP_BUILD_VARIANT_SNAFU)) != 1
#error "Exactly one LAMP_BUILD_VARIANT_* must be defined (check platformio.ini env)"
#endif

namespace lamp {

class Lamp;

inline std::unique_ptr<Lamp> createCompiledLamp() {
#ifdef LAMP_BUILD_VARIANT_STANDARD
  return std::make_unique<StandardLamp>();
#elif defined(LAMP_BUILD_VARIANT_SNAFU)
  return std::make_unique<SnafuLamp>();
#endif
}

inline const char* compiledLampType() {
#ifdef LAMP_BUILD_VARIANT_STANDARD
  return "standard";
#elif defined(LAMP_BUILD_VARIANT_SNAFU)
  return "snafu";
#endif
}

}  // namespace lamp
