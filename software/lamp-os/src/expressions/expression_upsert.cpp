#include "expressions/expression_upsert.hpp"

#include "config/config_codec.hpp"

namespace lamp {

UpsertVerdict admitUpsert(const ExpressionRegistry& registry, const ExpressionConfig& cfg,
                          ExpressionSettings* settings) {
  // An unregistered type never renders, and an internal one plays only when
  // the lamp fires it; storing either only spends a record and NVS.
  if (!registry.configurable(cfg.type.c_str())) return UpsertVerdict::NotConfigurable;
  if (cfg.target < kExpressionTargetMin || cfg.target > kExpressionTargetMax) {
    return UpsertVerdict::BadTarget;
  }
  if (!settings) return UpsertVerdict::Live;
  return config_codec::mirrorUpsert(*settings, cfg) ? UpsertVerdict::Saved
                                                    : UpsertVerdict::Unsaved;
}

}  // namespace lamp
