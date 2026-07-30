#pragma once

#include <cstdint>

#include "config/config_types.hpp"  // SocialMode

// The shared social-openness ladder. One relationship ordering (disposition ×
// mode) drives both the greeting-profile picker and bid acceptance:
//
//   Snub(0) < PartialSnub(1) < Minimal(2) < Standard(3) < Gentle(4)
//           < Warm(5) < Enthused(6) < Effusive(7)
//
// socialTier is the single source for this ordering; personality_engine's
// profileFor derives its greeting profile from the tier (profileForTier maps
// each rung 1:1). The cold end and Extrovert-Neutral clamp are preserved
// deliberately so the greeting mapping stays lossless.
//
// Header-only as a native-test seam (the receiver + equivalence tests link it
// without the firmware).

namespace lamp {

// Ladder index [0..7] for a disposition (1..5; unknown/other → Neutral) and
// social mode.
constexpr uint8_t socialTier(uint8_t disposition, SocialMode mode) {
  switch (disposition) {
    case 1:  // Salty
      return 0;  // Snub
    case 2:  // Wary
      return 1;  // PartialSnub
    case 4:  // Fond
      switch (mode) {
        case SocialMode::Introvert: return 4;  // Gentle
        case SocialMode::Ambivert:  return 5;  // Warm
        case SocialMode::Extrovert: return 6;  // Enthused
      }
      return 5;
    case 5:  // Smitten
      switch (mode) {
        case SocialMode::Introvert: return 5;  // Warm
        case SocialMode::Ambivert:  return 6;  // Enthused
        case SocialMode::Extrovert: return 7;  // Effusive
      }
      return 6;
    case 3:  // Neutral (also the default for unknown peers)
    default:
      switch (mode) {
        case SocialMode::Introvert: return 2;  // Minimal
        case SocialMode::Ambivert:  return 3;  // Standard
        case SocialMode::Extrovert: return 3;  // Standard (clamped)
      }
      return 3;
  }
}

// Bid acceptance probability [0..100] for (disposition, mode): the ladder tier
// mapped through the acceptance curve. Unknown peer → Neutral (disposition 3),
// matching getDisposition.
constexpr uint8_t socialBidResponse(uint8_t disposition, SocialMode mode) {
  constexpr uint8_t kTierAccept[8] = {0, 0, 15, 30, 45, 60, 80, 100};
  return kTierAccept[socialTier(disposition, mode)];
}

}  // namespace lamp
