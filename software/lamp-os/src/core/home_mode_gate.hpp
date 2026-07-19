#pragma once

#include <string>
#include <vector>

namespace lamp {

// Pure membership test for the compositor home-mode skip decision.
// No Compositor or AnimatedBehavior deps, usable in native tests directly.
inline bool homeModeExpressionSkips(const char* id,
                                    const std::vector<std::string>& disabled) {
  if (!id) return false;
  for (const auto& s : disabled) {
    if (s == id) return true;
  }
  return false;
}

}  // namespace lamp
