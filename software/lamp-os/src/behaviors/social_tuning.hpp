#pragma once

#include <cstdint>

#include "behaviors/copycat.hpp"
#include "behaviors/social_reunion.hpp"
#include "behaviors/yawn_contagion.hpp"
#include "core/fondness_engine.hpp"

namespace lamp {

// Every social feel-number table the serial tuning commands reach.
struct SocialTuningSet {
  FondnessTuning& fondness;
  ContagionTuning& contagion;
  ReunionTuning& reunion;
  YawnTuning& yawn;
};

// Sets `key` for the `<scope>.set <key> <value>` command. Scope `fond` reaches
// every table, and the aliases `copy`, `reunion` and `yawn` only
// ContagionTuning, ReunionTuning and YawnTuning. False, every table unchanged,
// for an unknown scope or key or a value out of range.
bool setSocialTuning(const SocialTuningSet& t, const char* scope, const char* key,
                     uint32_t value);

}  // namespace lamp
