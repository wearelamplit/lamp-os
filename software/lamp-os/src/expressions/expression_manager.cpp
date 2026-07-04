#include "expression_manager.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

#include "components/network/protocol/lamp_protocol.hpp"
#include "components/network/mesh/nearby_lamps.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"

namespace lamp {

void ExpressionManager::begin(FrameBuffer* shade, FrameBuffer* base) {
  shadeBuffer = shade;
  baseBuffer = base;

  // Publish the frame buffers into the shared BehaviorContext, replacing the
  // previous `expressionFrameBuffers` extern vector. If the compositor has
  // already been wired (setCompositor was called before begin()), publish now;
  // otherwise setCompositor will publish.
  if (compositor_) {
    auto& ctx = compositor_->behaviorContext();
    ctx.expressionFrameBuffers.clear();
    ctx.expressionFrameBuffers.push_back(shade);
    ctx.expressionFrameBuffers.push_back(base);
  }
}

void ExpressionManager::loadFromConfig(const ExpressionSettings& settings) {
  clear();

  for (const auto& config : settings.expressions) {
    addExpression(config);
  }
}


// Factory: build an Expression subclass instance for `type` bound to `buffer`,
// configured with the given palette / interval range / target / params.
// Returns nullptr for unknown types. Shared by addExpression (configured
// entries) and triggerInvocation (transient one-shots from remote cascades) —
// neither path needs to know the per-subclass constructor arguments.
static std::unique_ptr<Expression> makeExpression(
    const std::string& type, FrameBuffer* buffer,
    const std::vector<Color>& colors,
    uint32_t intervalMin, uint32_t intervalMax,
    ExpressionTarget target,
    const std::map<std::string, uint32_t>& parameters) {
  std::unique_ptr<Expression> expr;
  if (type == "glitchy") {
    auto e = std::make_unique<GlitchyExpression>(buffer, 3);
    e->configure(colors, intervalMin, intervalMax, target);
    e->configureFromParameters(parameters);
    expr = std::move(e);
  } else if (type == "shifty") {
    auto e = std::make_unique<ShiftyExpression>(buffer, 120);
    e->configure(colors, intervalMin, intervalMax, target);
    e->configureFromParameters(parameters);
    expr = std::move(e);
  } else if (type == "pulse") {
    auto e = std::make_unique<PulseExpression>(buffer, 60);
    e->configure(colors, intervalMin, intervalMax, target);
    e->configureFromParameters(parameters);
    expr = std::move(e);
  } else if (type == "breathing") {
    auto e = std::make_unique<BreathingExpression>(buffer, 60);
    e->configure(colors, intervalMin, intervalMax, target);
    e->configureFromParameters(parameters);
    expr = std::move(e);
  }
  return expr;
}

void ExpressionManager::addExpression(const ExpressionConfig& config) {
  if (!shadeBuffer || !baseBuffer) return;

  auto target = static_cast<ExpressionTarget>(config.target);

  // Determine target buffers
  std::vector<FrameBuffer*> targetBuffers;
  if (target == TARGET_BOTH) {
    targetBuffers = {shadeBuffer, baseBuffer};
  } else {
    targetBuffers = {(target == TARGET_SHADE) ? shadeBuffer : baseBuffer};
  }

  // Create expressions for each target buffer
  for (auto* buffer : targetBuffers) {
    if (auto expr = makeExpression(config.type, buffer, config.colors,
                                   config.intervalMin, config.intervalMax,
                                   target, config.parameters)) {
      expr->autoTriggerEnabled = config.enabled;
      expressions.push_back({std::move(expr), config.type, config});
    }
  }
}

void ExpressionManager::setMeshLink(MeshLink* link) {
  meshLink_ = link;
}

void ExpressionManager::setCompositor(Compositor* compositor) {
  compositor_ = compositor;
  if (!compositor_) return;
  // Publish ourselves and (if begin() already ran) the frame buffer list
  // into the compositor's BehaviorContext so registered behaviors can reach
  // both without a global. Safe to call before or after begin().
  auto& ctx = compositor_->behaviorContext();
  ctx.expressionManager = this;
  if (shadeBuffer && baseBuffer) {
    ctx.expressionFrameBuffers.clear();
    ctx.expressionFrameBuffers.push_back(shadeBuffer);
    ctx.expressionFrameBuffers.push_back(baseBuffer);
  }
}

void ExpressionManager::maybeCascade(const ExpressionEntry& entry) {
  if (!meshLink_ || !entry.expression) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: skip (no meshLink=%d no expression=%d)\n",
                  entry.config.type.c_str(), !meshLink_, !entry.expression);
#endif
    return;
  }
  if (entry.config.getParameter(kParamCascadeEnabled, 0) == 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: skip (cascadeEnabled=0)\n",
                  entry.config.type.c_str());
#endif
    return;
  }

  // "Cascade once per logical trigger" invariant. A TARGET_BOTH expression
  // has TWO entries (shade + base) that fire independently from
  // Expression::control() in the same loop tick; each calls
  // onExpressionFired -> maybeCascade. Without this gate, receivers see
  // two cascade invocations and the mesh does 2x the work for one logical
  // trigger. Keying on (type, intervalIdx=target) is enough because both
  // halves of a TARGET_BOTH config share the same target value, while a
  // separate TARGET_SHADE-only entry of the same type has a distinct
  // target and still gets its own cascade.
  const uint32_t intervalIdx = static_cast<uint32_t>(entry.expression->getTarget());
  const uint32_t nowMs = millis();
  if (recentCascades_.seen(entry.config.type, intervalIdx, nowMs)) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: skip (RecentCascade dedup, target=%u)\n",
                  entry.config.type.c_str(), (unsigned)intervalIdx);
#endif
    return;  // dedup: same logical trigger already cascaded
  }
  recentCascades_.record(entry.config.type, intervalIdx, nowMs);

  const uint32_t staggerMs = entry.config.getParameter(kParamCascadeStaggerMs, 0);

  // Build the invocation payload (JSON, serialised once).
  ExpressionInvocation inv;
  inv.type = entry.config.type;
  inv.colors = entry.expression->getColors();
  inv.target = static_cast<uint8_t>(entry.expression->getTarget());
  inv.parameters = parametersWithoutCascadeKeys(entry.config.parameters);
  // inv.delayMs stays 0 — per-peer stagger rides the wire as the
  // MSG_EVENT staggerEntries list, NOT inside the JSON payload. Receivers
  // pull their own delayMs from the stagger list at recv time.

  std::string json;
  serializeInvocation(inv, json);

  // Build the RSSI-sorted stagger list FIRST — the payload budget depends
  // on how many stagger entries actually ride the wire (small meshes have
  // more headroom). Take a snapshot, filter out self + peers with no MAC,
  // sort by lastRssi descending (strongest signal = physically closest →
  // fires earliest in the wave), cap at kMaxStaggerEntries so the wire
  // frame stays bounded.
  auto peers = nearbyLamps.getReachableViaEspNow(LAMP_PRUNE_TIME_MS);
  uint8_t myMac[6];
  meshLink_->getMyMac(myMac);
  std::vector<NearbyLamp> targets;
  targets.reserve(peers.size());
  for (const auto& p : peers) {
    if (!p.hasMac) continue;
    if (std::memcmp(p.mac, myMac, 6) == 0) continue;
    targets.push_back(p);
  }
  std::sort(targets.begin(), targets.end(),
            [](const NearbyLamp& a, const NearbyLamp& b) {
              // Higher RSSI = stronger signal → fires first. Unknown
              // (-127) sorts to the back automatically.
              return a.lastRssi > b.lastRssi;
            });
  if (targets.size() > lamp_protocol::kMaxStaggerEntries) {
    targets.resize(lamp_protocol::kMaxStaggerEntries);
  }
  const uint8_t numStagger = static_cast<uint8_t>(targets.size());

  // Dynamic payload budget against the actual stagger count. 2-lamp meshes
  // get ~226 B of headroom; fully-populated (12-peer) meshes get the
  // worst-case 138. Without this, the static 138 cap dropped any glitchy
  // cascade (~151 B real payload) even on a 1-peer mesh — silent failure
  // observed in the field, see live trace 2026-06-03.
  const size_t maxPayload = lamp_protocol::maxEventPayloadFor(numStagger);
  if (json.size() > maxPayload) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: payload %u > max %u (peers=%u), dropping\n",
                  entry.config.type.c_str(),
                  (unsigned)json.size(),
                  (unsigned)maxPayload,
                  (unsigned)numStagger);
#endif
    return;
  }

  // Pack the wire-shape arrays. Each per-peer delay = (i + 1) * staggerMs,
  // clamped to kMaxDelayMs defensively. i fits in uint8_t (we capped at 12).
  //
  // The (i + 1) offset means the closest peer (i=0) fires staggerMs AFTER
  // the sender — not at the same instant. Without the offset, a 2-lamp
  // mesh fires both lamps simultaneously regardless of staggerMs, which
  // defeats the "wave from the trigger source outward" UX intent.
  // Observed 2026-06-03: with staggerMs=800 and 1 peer, melonie fired
  // at t=0 alongside jacko instead of at t=800ms.
  uint8_t staggerMacs[lamp_protocol::kMaxStaggerEntries * 6];
  uint16_t staggerDelays[lamp_protocol::kMaxStaggerEntries];
  for (uint8_t i = 0; i < numStagger; ++i) {
    std::memcpy(&staggerMacs[i * 6], targets[i].mac, 6);
    const uint32_t d = static_cast<uint32_t>(i + 1) * staggerMs;
    staggerDelays[i] = static_cast<uint16_t>(
        d > kMaxDelayMs ? kMaxDelayMs : d);
  }

#ifdef LAMP_DEBUG
  Serial.printf("[cascade] %s: broadcasting MSG_EVENT (target=%u staggerMs=%u peers=%u payload=%u/%u)\n",
                entry.config.type.c_str(), (unsigned)intervalIdx,
                (unsigned)staggerMs, (unsigned)numStagger,
                (unsigned)json.size(), (unsigned)maxPayload);
  for (uint8_t i = 0; i < numStagger; ++i) {
    Serial.printf("[cascade]   peer rssi=%d delayMs=%u\n",
                  (int)targets[i].lastRssi, (unsigned)staggerDelays[i]);
  }
#endif

  // Build the MSG_EVENT frame and broadcast it twice, back-to-back. ESP-NOW
  // broadcasts have no link-layer ACK, so the duplicate is best-effort
  // insurance against a single dropped frame (BLE adv burst, brief radio
  // contention, etc.). Both copies share the same seq; receivers'
  // eventDedup_ collapses the second copy by (sourceMac, seq) so the
  // dispatch only fires once.
  //
  // The previous implementation inserted `delay(20)` between the two
  // sends to space them across separate RF transient windows. That was
  // dropped 2026-06-03 because the delay() blocked Core 1 and stalled the
  // sender's compositor render — sender's own LEDs visibly lagged
  // receivers'. Back-to-back (no delay) loses the across-RF-window
  // spread but keeps the "two TX attempts" resilience and doesn't block.
  uint8_t frame[lamp_protocol::EVENT_MAX_SIZE];
  const uint16_t seq = meshLink_->nextEventSeq();
  const size_t frameLen = lamp_protocol::buildEvent(
      frame, sizeof(frame), seq, myMac,
      static_cast<uint8_t>(lamp_protocol::EventKind::ExpressionTriggered),
      staggerMacs, staggerDelays, numStagger,
      reinterpret_cast<const uint8_t*>(json.data()),
      static_cast<uint16_t>(json.size()));
  if (frameLen == 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: buildEvent failed (frameLen=0)\n",
                  entry.config.type.c_str());
#endif
    return;
  }
  // Mesh quiesce during gossip OTA: a 1.4 MB chunk stream is fragile under
  // BLE coex; suppress MSG_EVENT cascade broadcasts while we're mid-flow
  // so the chunk stream gets the channel time. The cascade resumes once
  // the OTA flow exits (Done/Failed → Idle).
  if (meshLink_->isOtaInProgress()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] %s: suppressed during OTA flow\n",
                  entry.config.type.c_str());
#endif
    return;
  }
  meshLink_->broadcastRaw(frame, frameLen);
  meshLink_->broadcastRaw(frame, frameLen);
}

std::vector<AnimatedBehavior*> ExpressionManager::getBehaviors() {
  std::vector<AnimatedBehavior*> behaviors;
  for (auto& entry : expressions) {
    behaviors.push_back(entry.expression.get());
  }
  return behaviors;
}

void ExpressionManager::clear() {
  // Drop active-test pointers first — they point into the entries we're
  // about to destroy.
  activeTests_.clear();
  expressions.clear();
}

bool ExpressionManager::triggerExpression(const std::string& type) {
  bool triggered = false;
  const ExpressionEntry* firstFired = nullptr;
  // Suppress per-entry cascade callbacks from Expression::trigger() — we
  // batch a single cascade for the logical trigger after the loop.
  suppressCascade_ = true;
  for (auto& entry : expressions) {
    if (entry.type == type && entry.expression) {
      entry.expression->trigger();
      triggered = true;
      if (!firstFired) firstFired = &entry;
    }
  }
  suppressCascade_ = false;
#ifdef LAMP_DEBUG
  Serial.printf("[trigger] '%s' fired=%d (matched %s)\n",
                type.c_str(), triggered,
                firstFired ? "≥1 entry" : "no entries");
#endif
  // Cascade once per logical trigger, not once per entry — a TARGET_BOTH
  // expression has two entries (shade + base) but should fan out a single
  // invocation that receivers' own managers expand back to both sides.
  if (firstFired) maybeCascade(*firstFired);
  return triggered;
}

bool ExpressionManager::triggerExpression(const std::string& type, ExpressionTarget target) {
  bool triggered = false;
  const ExpressionEntry* firstFired = nullptr;
  suppressCascade_ = true;
  for (auto& entry : expressions) {
    if (entry.type == type && entry.expression && entry.expression->getTarget() == target) {
      entry.expression->trigger();
      triggered = true;
      if (!firstFired) firstFired = &entry;
    }
  }
  suppressCascade_ = false;
  if (firstFired) maybeCascade(*firstFired);
  return triggered;
}

void ExpressionManager::onExpressionFired(Expression* e) {
  if (suppressCascade_ || !e) return;
  for (auto& entry : expressions) {
    if (entry.expression.get() == e) {
      maybeCascade(entry);
      return;
    }
  }
}

bool ExpressionManager::triggerInvocation(const ExpressionInvocation& inv,
                                          const uint8_t srcMac[6]) {
  if (!shadeBuffer || !baseBuffer) return false;

  ExpressionTarget invTarget = static_cast<ExpressionTarget>(inv.target);

  // Coalesce: if a transient with the same (sender, type) is still
  // animating, drop this incoming cascade. Prevents pile-up from one
  // chatty sender (e.g. spam-tapping Test on the originator) while still
  // letting concurrent cascades from DIFFERENT senders both land — each
  // sender contributes one in-flight transient.
  for (const auto& t : transientExpressions_) {
    if (t.type == inv.type && t.expression &&
        std::memcmp(t.srcMac, srcMac, 6) == 0 &&
        !t.expression->isAnimationComplete()) {
#ifdef LAMP_DEBUG
      Serial.printf("[expr] coalesce %s from %02X:%02X:%02X:%02X:%02X:%02X (in flight)\n",
                    inv.type.c_str(),
                    srcMac[0], srcMac[1], srcMac[2],
                    srcMac[3], srcMac[4], srcMac[5]);
#endif
      return false;
    }
  }

  // Determine target buffers — same convention as addExpression. TARGET_BOTH
  // fires on both halves of the lamp; specific target fires on one.
  std::vector<FrameBuffer*> targetBuffers;
  if (invTarget == TARGET_BOTH) {
    targetBuffers = {shadeBuffer, baseBuffer};
  } else if (invTarget == TARGET_SHADE) {
    targetBuffers = {shadeBuffer};
  } else if (invTarget == TARGET_BASE) {
    targetBuffers = {baseBuffer};
  } else {
    return false;
  }

  // Loop-break invariant: the transient's trigger() will call
  // onExpressionFired via the global manager pointer; suppress so a
  // remote-received trigger can never re-cascade. Receivers are terminal in
  // the propagation graph.
  suppressCascade_ = true;

  bool triggered = false;
  for (auto* buffer : targetBuffers) {
    // Build a fresh one-shot Expression instance directly from the
    // invocation. NEVER consults this lamp's `expressions` (configured)
    // vector — the receiver's local config is intentionally irrelevant.
    // The cascade is a self-contained "execute this expression once and
    // forget it" command; the receiver's own configured expressions remain
    // entirely independent (untouched, unread, unmodified).
    auto expr = makeExpression(inv.type, buffer, inv.colors,
                               /*intervalMin*/ 60, /*intervalMax*/ 900,
                               invTarget, inv.parameters);
    if (!expr) continue;  // unknown type
    expr->autoTriggerEnabled = false;  // pure one-shot — never re-fires itself

    Expression* raw = expr.get();
    if (compositor_) compositor_->addBehavior(raw);
    TransientExpression t;
    t.type = inv.type;
    std::memcpy(t.srcMac, srcMac, 6);
    t.expression = std::move(expr);
    t.createdMs = millis();
    transientExpressions_.push_back(std::move(t));
    raw->trigger();
    triggered = true;
  }

  suppressCascade_ = false;
  return triggered;
}

// Evict a one-shot cascade even when isAnimationComplete() never fires; a
// stuck completion signal would otherwise latch it on the compositor forever.
static constexpr uint32_t kTransientMaxLifetimeMs = 180000;

void ExpressionManager::gcTransients() {
  if (transientExpressions_.empty()) return;
  const uint32_t nowMs = millis();
  for (auto it = transientExpressions_.begin(); it != transientExpressions_.end();) {
    const bool complete = it->expression && it->expression->isAnimationComplete();
    const bool expired = nowMs - it->createdMs >= kTransientMaxLifetimeMs;
    if (complete || expired) {
      if (compositor_) compositor_->removeBehavior(it->expression.get());
      it = transientExpressions_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<Color> ExpressionManager::getExpressionColors(const std::string& type) const {
  for (const auto& entry : expressions) {
    if (entry.type == type && entry.expression) {
      return entry.expression->getColors();
    }
  }
  return {};
}

void ExpressionManager::upsertExpression(const ExpressionConfig& config, Compositor* compositor) {
  ExpressionTarget target = static_cast<ExpressionTarget>(config.target);
  removeExpression(config.type, target, compositor);

  size_t prevCount = expressions.size();
  addExpression(config);

  if (compositor) {
    for (size_t i = prevCount; i < expressions.size(); i++) {
      compositor->addBehavior(expressions[i].expression.get());
    }
  }
}

bool ExpressionManager::markTestActive(const std::string& type, ExpressionTarget target) {
  const bool wasEmpty = activeTests_.empty();
  for (auto& entry : expressions) {
    if (entry.type != type || !entry.expression) continue;
    if (entry.expression->getTarget() != target) continue;
    Expression* raw = entry.expression.get();
    // Dedup: spam-tapping Test on the same entry shouldn't pile up the set.
    bool already = false;
    for (auto* e : activeTests_) {
      if (e == raw) { already = true; break; }
    }
    if (!already) activeTests_.push_back(raw);
  }
  return wasEmpty && !activeTests_.empty();
}

bool ExpressionManager::reapCompletedTests() {
  if (activeTests_.empty()) return false;
  for (auto it = activeTests_.begin(); it != activeTests_.end();) {
    Expression* e = *it;
    if (!e || e->animationState == STOPPED) {
      it = activeTests_.erase(it);
    } else {
      ++it;
    }
  }
  return activeTests_.empty();
}

bool ExpressionManager::clearAllTestActive() {
  if (activeTests_.empty()) return false;
  activeTests_.clear();
  return true;
}

void ExpressionManager::removeExpression(const std::string& type, ExpressionTarget target, Compositor* compositor) {
  for (auto it = expressions.begin(); it != expressions.end();) {
    if (it->type == type && it->expression && it->expression->getTarget() == target) {
      // Scrub the active-test set so reapCompletedTests can't dereference a
      // freed pointer after this destroy.
      Expression* dying = it->expression.get();
      for (auto ait = activeTests_.begin(); ait != activeTests_.end();) {
        if (*ait == dying) ait = activeTests_.erase(ait);
        else ++ait;
      }
      if (compositor) compositor->removeBehavior(it->expression.get());
      it = expressions.erase(it);
    } else {
      ++it;
    }
  }
}

// Cheap "is `key` present as a JSON string value" peek. Walks `payload`
// (NOT NUL-terminated; bounded by payloadLen) looking for `"key":"value"`
// and copies the value substring into `outValue` up to outMaxLen-1 chars.
// Returns true on match. Used by tryHandleExpressionEvent to extract the
// expression type without paying for a full JsonDocument parse — the
// payload may be dropped seconds later if the local lamp doesn't have
// that type cascaded.
//
// Doesn't handle escapes or whitespace inside the key/value — fine for
// our serializeInvocation output which is single-line ArduinoJson with
// no fancy formatting. Worst case a false negative leaves us with the
// full parse, same as the legacy path.
static bool peekJsonStringField(const uint8_t* payload, uint16_t payloadLen,
                                const char* key,
                                char* outValue, size_t outMaxLen) {
  if (!payload || !key || !outValue || outMaxLen == 0) return false;
  const size_t keyLen = std::strlen(key);
  // Need at minimum: `"key":""` which is keyLen + 6 chars (quotes + colon
  // + empty value pair). Guard so the index math below can't underflow.
  if (payloadLen < keyLen + 6) return false;
  // Scan for `"key":"`. Stop early enough that the lookahead can't read
  // off the end.
  const size_t needleLen = keyLen + 4;  // "key":"
  for (size_t i = 0; i + needleLen <= payloadLen; ++i) {
    if (payload[i] != '"') continue;
    if (std::memcmp(&payload[i + 1], key, keyLen) != 0) continue;
    if (payload[i + 1 + keyLen] != '"') continue;
    if (payload[i + 2 + keyLen] != ':') continue;
    if (payload[i + 3 + keyLen] != '"') continue;
    // Found it. Copy until the next unescaped `"`, capping at outMaxLen-1.
    size_t valueStart = i + 4 + keyLen;
    size_t w = 0;
    for (size_t j = valueStart; j < payloadLen && w + 1 < outMaxLen; ++j) {
      if (payload[j] == '"') {
        outValue[w] = '\0';
        return true;
      }
      outValue[w++] = static_cast<char>(payload[j]);
    }
    // Ran off the buffer without closing quote — bad payload.
    return false;
  }
  return false;
}

void ExpressionManager::tryHandleExpressionEvent(const uint8_t sourceMac[6],
                                                  uint16_t suppliedDelayMs,
                                                  const uint8_t* payload,
                                                  uint16_t payloadLen) {
  if (!payload || payloadLen == 0) return;

  // 1) Cheap peek for the type. Used as the RecentCascade dedup key so we
  // can drop a duplicate cascade of the same type without paying for the
  // full JSON parse. The JSON we emit always carries `type`; an attacker-
  // crafted MSG_EVENT without one is ignored silently.
  //
  // No local-config consult: cascade is sender-authoritative. The wire
  // payload carries the full ExpressionInvocation (type, target, colors,
  // parameters), and triggerInvocation builds a fresh transient Expression
  // from it. The receiver's own `expressions` vector is intentionally
  // irrelevant — keeps the cascade behavior matching the legacy CONTROL_OP
  // model ("execute this expression once and forget it"). A receiver-side
  // cascadeEnabled gate that filters on the receiver's own expressions
  // silently breaks this — don't reintroduce one.
  char type[64] = {0};
  if (!peekJsonStringField(payload, payloadLen, "type", type, sizeof(type))) {
#ifdef LAMP_DEBUG
    Serial.println("[event] no type peek, drop");
#endif
    return;
  }

  // 2) RecentCascade dedup. Keep the same key shape the local-trigger
  // path uses (type, target). We don't know `target` yet without the
  // full parse, but the wire format defines TARGET_BOTH as 3 and that's
  // what cascade events almost always carry. Use a per-type bucket
  // keyed at intervalIdx=0 to mirror the simpler remote-side dedup —
  // suppression scope is "same type from any sender within window."
  const uint32_t nowMs = millis();
  if (recentCascades_.seen(std::string(type), 0, nowMs)) {
#ifdef LAMP_DEBUG
    Serial.printf("[event] type=%s recent-cascade dedup\n", type);
#endif
    return;
  }
  recentCascades_.record(std::string(type), 0, nowMs);

  // 3) Full parse now that we've confirmed we'll act on it.
  JsonDocument doc;
  if (deserializeJson(doc, payload, payloadLen) != DeserializationError::Ok) {
#ifdef LAMP_DEBUG
    Serial.println("[event] full parse failed");
#endif
    return;
  }
  ExpressionInvocation inv;
  if (!parseInvocation(doc.as<JsonObjectConst>(), inv)) {
#ifdef LAMP_DEBUG
    Serial.println("[event] parseInvocation failed");
#endif
    return;
  }

  // 4) Defense-in-depth: clamp the supplied delay. parseInvocation clamps
  // `inv.delayMs` but the stagger-list-delivered delay is a separate
  // attacker-controlled quantity. kMaxDelayMs (10s) is plenty for any
  // realistic cascade UX.
  const uint32_t delayMs = clampDelayMs(static_cast<uint32_t>(suppliedDelayMs));
  if (delayMs == 0) {
    triggerInvocation(inv, sourceMac);
  } else {
    enqueueDelayedInvocation(inv, sourceMac, delayMs);
  }
}

}  // namespace lamp