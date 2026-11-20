#pragma once

#include <cstdint>

#include "config/config_types.hpp"
#include "expressions/expression_registry.hpp"

namespace lamp {

enum class UpsertVerdict : uint8_t {
  Saved,            // admitted and mirrored into the settings
  Unsaved,          // admitted, but the settings are full
  Live,             // admitted with no settings to mirror into
  NotConfigurable,  // a type this lamp does not register, or an internal one
  BadTarget,
};

// Admits a person's expression upsert and, given `settings`, mirrors it there
// so the next save keeps it. A refusal leaves `settings` unchanged. Every
// person's upsert passes through here before it reaches the manager.
UpsertVerdict admitUpsert(const ExpressionRegistry& registry, const ExpressionConfig& cfg,
                          ExpressionSettings* settings);

}  // namespace lamp
