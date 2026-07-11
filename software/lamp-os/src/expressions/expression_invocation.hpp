#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "util/color.hpp"

namespace lamp {

// A one-shot "fire this expression now" payload. Subset of ExpressionConfig
// (no enabled / intervalMin / intervalMax). Carried as the JSON body of
// MSG_COMMAND (cascade + greeting) and MSG_EVENT (announce) frames.
//
// `colors` is an optional palette override — empty means "use the configured
// palette for this type." `delayMs` is interpreted by the receiver as
// "wait this long after the message arrives, then trigger." Senders use it
// to stagger a fan-out across multiple peers without needing a shared clock.
struct ExpressionInvocation {
  std::string type;
  std::vector<Color> colors;
  uint8_t target = 3;  // 1=SHADE, 2=BASE, 3=BOTH
  std::map<std::string, uint32_t> parameters;
  uint32_t delayMs = 0;
};

// Cascade convention: the manager fans out any locally-triggered expression
// whose parameters map contains cascadeEnabled=1, using cascadeStaggerMs as
// the inter-peer delay. These keys are stripped from the invocation that
// goes on the wire so receivers can never re-cascade.
constexpr const char* kParamCascadeEnabled = "cascadeEnabled";
constexpr const char* kParamCascadeStaggerMs = "cascadeStaggerMs";

// Upper bound (ms) we'll accept for a remote-triggered `delayMs`. ESP-NOW
// peers are untrusted: an unclamped uint32_t can schedule a fire ~49 days
// out, which holds a pendingTriggers slot indefinitely — 16 such payloads
// from a rogue peer would wedge the queue. 10 s comfortably exceeds typical
// cascade staggers (<2 s) while keeping a full queue self-clearing in
// seconds, not weeks. Applied in parseInvocation via clampDelayMs().
constexpr uint32_t kMaxDelayMs = 10000;

// Clamp `v` to [0, kMaxDelayMs]. Pure; safe to call on attacker-controlled
// values. Exposed for native unit testing.
uint32_t clampDelayMs(uint32_t v);

// Build a copy of `parameters` with the cascade-control keys removed.
// Used by ExpressionManager when serializing an invocation for the wire —
// receivers should never see the cascade keys, both to keep the message
// small and as defense-in-depth against accidental re-cascade.
std::map<std::string, uint32_t> parametersWithoutCascadeKeys(
    const std::map<std::string, uint32_t>& parameters);

// Serialize `inv` to JSON for MSG_COMMAND and MSG_EVENT payloads. `out` is
// set to the serialized string. Always succeeds.
void serializeInvocation(const ExpressionInvocation& inv, std::string& out);

// Parse an ExpressionInvocation payload (an ArduinoJson object extracted from
// an incoming MSG_COMMAND or MSG_EVENT frame) into `out`. Returns false if
// `type` is missing or empty. Unknown keys are ignored. `colors` uses
// "#RRGGBBWW" hex strings.
bool parseInvocation(JsonObjectConst doc, ExpressionInvocation& out);

}  // namespace lamp
