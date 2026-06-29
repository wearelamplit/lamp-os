// software/lamp-os/src/core/lamp_features.hpp
#pragma once
#include <cstdint>

namespace lamp {

// Bitmask of built-in behaviors. A Lamp subclass returns the subset it
// wants the framework to construct + register. Subclasses replacing a
// built-in mask it out and add their own AnimatedBehavior in
// createBehaviors().
enum class Features : uint32_t {
  None                = 0,
  SocialBehavior      = 1u << 0,
  DefaultExpressions  = 1u << 1,  // load Expressions from NVS on boot
  // Bits 2-4 unused.
  FadeOutBehavior     = 1u << 5,
  KnockoutBehavior    = 1u << 6,
  All                 = 0xFFFFFFFFu,
};

inline Features operator|(Features a, Features b) {
  return static_cast<Features>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline Features operator&(Features a, Features b) {
  return static_cast<Features>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline Features operator~(Features a) {
  return static_cast<Features>(~static_cast<uint32_t>(a));
}
inline bool any(Features f, Features mask) {
  return (static_cast<uint32_t>(f) & static_cast<uint32_t>(mask)) != 0;
}

}  // namespace lamp
