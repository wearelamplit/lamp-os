// software/lamp-os/src/components/apply/apply_expressions.hpp
//
// Expression-op user/remote split. See apply_brightness.hpp for the
// general split rationale.

#pragma once

#include <ArduinoJson.h>

namespace lamp {

void runExpressionOp(JsonObject doc, bool mutateConfig);

namespace apply {

inline void expressionOpToConfig(JsonObject doc) {
  ::lamp::runExpressionOp(doc, /*mutateConfig=*/true);
}

inline void expressionOpToRender(JsonObject doc) {
  ::lamp::runExpressionOp(doc, /*mutateConfig=*/false);
}

}  // namespace apply
}  // namespace lamp
