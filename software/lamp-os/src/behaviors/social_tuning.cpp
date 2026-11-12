#include "behaviors/social_tuning.hpp"

#include <cstring>

namespace lamp {

bool setSocialTuning(const SocialTuningSet& t, const char* scope, const char* key,
                     uint32_t value) {
  const bool fond = std::strcmp(scope, "fond") == 0;
  if (fond && setFondnessTuning(t.fondness, key, value)) return true;
  if ((fond || std::strcmp(scope, "copy") == 0) && setContagionTuning(t.contagion, key, value)) {
    return true;
  }
  if ((fond || std::strcmp(scope, "reunion") == 0) && setReunionTuning(t.reunion, key, value)) {
    return true;
  }
  return (fond || std::strcmp(scope, "yawn") == 0) && setYawnTuning(t.yawn, key, value);
}

}  // namespace lamp
