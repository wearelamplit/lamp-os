#pragma once
#include <map>
#include <string>
#include <vector>

#include "expressions/expression_schema.hpp"

namespace lamp {

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
