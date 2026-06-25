// software/lamp-os/src/components/apply/apply_base_ac.hpp
#pragma once

#include "config/config.hpp"

namespace lamp {
class Config;
}
extern lamp::Config config;

namespace lamp {
namespace apply {

inline void baseAcLocal(int ac) {
  if (ac < 0) return;
  // Cap at colors.size()-1 if larger (silently clamps a stale write).
  size_t maxIdx = ::config.base.colors.empty()
                      ? 0
                      : ::config.base.colors.size() - 1;
  if (static_cast<size_t>(ac) > maxIdx) ac = static_cast<int>(maxIdx);
  ::config.base.ac = static_cast<uint8_t>(ac);
}

}  // namespace apply
}  // namespace lamp
