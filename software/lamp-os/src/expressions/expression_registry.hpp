#pragma once
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "expressions/expression_schema.hpp"
#include "expressions/param_utils.hpp"

namespace lamp {

// Effective continuity for a configured instance: an expression that exposes
// the loop param follows its config value (Continuous == 1); otherwise the
// descriptor's static continuous flag decides.
inline bool effectiveContinuous(const ExpressionDescriptor& d,
                                const std::map<std::string, uint32_t>& params) {
  for (const auto& p : d.params) {
    if (std::strcmp(p.key, kLoopParamKey) == 0) {
      return getParam(params, kLoopParamKey, 0) != 0;
    }
  }
  return d.continuous;
}

// Registry of ExpressionDescriptors. Holds pointers into static constexpr
// storage; descriptors are never copied.
class ExpressionRegistry {
 public:
  // Registers d. If an entry with the same id already exists, replaces it.
  void add(const ExpressionDescriptor& d);
  void remove(const char* id);
  const ExpressionDescriptor* find(const char* id) const;
  const std::vector<const ExpressionDescriptor*>& all() const;

  // Fills missing param keys from each ParamSpec's resolved default and folds
  // interval/duration range defaults into their min/maxKey. Never overwrites a
  // present key.
  void applyDefaults(const ExpressionDescriptor& d,
                     std::map<std::string, uint32_t>& params,
                     uint16_t window) const;

  // Returns the exprcat wire JSON: { "schemaVersion":1, "expressions":[...] }.
  std::string serializeCatalog() const;

 private:
  std::vector<const ExpressionDescriptor*> descriptors_;
};

}  // namespace lamp
