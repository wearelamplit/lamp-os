#include "expression_manager.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

#include "components/network/protocol/lamp_protocol.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"

// Owned by lamp.cpp; the private lamp_internal.hpp extern isn't includable
// from here, so mirror the declaration like ble_control.cpp does.
extern lamp::ExpressionManager expressionManager;

namespace lamp {

const std::string& expressionCatalogJson() {
  static const std::string cat = expressionManager.registry().serializeCatalog();
  return cat;
}

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


std::unique_ptr<Expression> ExpressionManager::makeExpression(
    const std::string& type, FrameBuffer* buffer,
    const std::vector<Color>& colors,
    uint32_t intervalMin, uint32_t intervalMax,
    ExpressionTarget target,
    const std::map<std::string, uint32_t>& parameters) {
  const ExpressionDescriptor* d = registry_.find(type.c_str());
  if (!d || !d->make) return nullptr;

  std::unique_ptr<Expression> expr(d->make(buffer));
  expr->configure(colors, intervalMin, intervalMax, target);

  std::map<std::string, uint32_t> effective = parameters;
  registry_.applyDefaults(*d, effective,
                          (buffer && buffer->pixelCount > 0) ? buffer->pixelCount : 1);
  expr->configureFromParameters(effective);
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

void ExpressionManager::maybeCascade(const ExpressionEntry& entry,
                                     const uint8_t* excludeMac) {
  if (!meshLink_ || !entry.expression) return;
  if (entry.config.getParameter(kParamCascadeEnabled, 0) == 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip not-enabled type=%s\n", entry.config.type.c_str());
#endif
    return;
  }

  // Continuous expressions retrigger at boot, settings upsert, and wisp
  // release; a stored cascadeEnabled=1 would fan out one-shots on each. The
  // app hides the cascade toggle for continuous types, so gate here to match.
  const ExpressionDescriptor* d = registry_.find(entry.config.type.c_str());
  if (d && effectiveContinuous(*d, entry.config.parameters)) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip continuous type=%s\n", entry.config.type.c_str());
#endif
    return;
  }

  const uint32_t intervalIdx = static_cast<uint32_t>(entry.expression->getTarget());
  const uint32_t nowMs = millis();
  if (recentCascades_.seen(entry.config.type, intervalIdx, nowMs)) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip dedup type=%s\n", entry.config.type.c_str());
#endif
    return;
  }
  recentCascades_.record(entry.config.type, intervalIdx, nowMs);

  const uint32_t staggerMs = entry.config.getParameter(kParamCascadeStaggerMs, 0);

  ExpressionInvocation inv;
  inv.type = entry.config.type;
  inv.colors = entry.expression->getColors();
  inv.target = static_cast<uint8_t>(entry.expression->getTarget());
  inv.parameters = parametersWithoutCascadeKeys(entry.config.parameters);

  auto peers = lampRoster.getMesh(LAMP_PRUNE_TIME_MS);
  uint8_t myMac[6];
  meshLink_->getMyMac(myMac);
  std::vector<RosterEntry> targets;
  targets.reserve(peers.size());
  for (const auto& p : peers) {
    if (!p.hasMac) continue;
    if (std::memcmp(p.mac, myMac, 6) == 0) continue;
    if (excludeMac && std::memcmp(p.mac, excludeMac, 6) == 0) continue;
    targets.push_back(p);
  }
  std::sort(targets.begin(), targets.end(),
            [](const RosterEntry& a, const RosterEntry& b) {
              return a.lastRssi > b.lastRssi;
            });

  if (targets.empty()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip no-peers type=%s\n", entry.config.type.c_str());
#endif
    return;
  }
  if (meshLink_->isOtaInProgress()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip ota type=%s\n", entry.config.type.c_str());
#endif
    return;
  }

  size_t sent = 0;
  size_t dropped = 0;
  size_t overLen = 0;
  for (size_t i = 0; i < targets.size(); ++i) {
    const uint32_t d = static_cast<uint32_t>(i + 1) * staggerMs;
    inv.delayMs = d > kMaxDelayMs ? kMaxDelayMs : d;
    std::string json;
    serializeInvocation(inv, json);
    if (json.size() > lamp_protocol::COMMAND_MAX_PAYLOAD) {
      ++dropped;
      overLen = json.size();
      continue;
    }
    meshLink_->sendCommand(targets[i].mac,
                           reinterpret_cast<const uint8_t*>(json.data()),
                           json.size());
    ++sent;
  }
  // Unconditional: an over-cap invocation silently fails to cascade on
  // deployed lamps; this is the only signal.
  if (dropped > 0) {
    Serial.printf("[cascade] ERR payload %uB over %uB cap, dropped %u/%u sends type=%s\n",
                  (unsigned)overLen,
                  (unsigned)lamp_protocol::COMMAND_MAX_PAYLOAD,
                  (unsigned)dropped, (unsigned)targets.size(),
                  entry.config.type.c_str());
  }
#ifdef LAMP_DEBUG
  if (sent > 0) {
    Serial.printf("[cascade] sent type=%s peers=%u\n",
                  entry.config.type.c_str(), (unsigned)sent);
  }
#endif
}

void ExpressionManager::emitEvent(const ExpressionEntry& entry) {
  if (!meshLink_ || !entry.expression) return;
  if (meshLink_->isOtaInProgress()) return;

  // Continuous expressions retrigger at boot, settings upsert, and wisp
  // release; announcing those would spuriously trigger peer mirrors.
  const ExpressionDescriptor* d = registry_.find(entry.config.type.c_str());
  if (d && effectiveContinuous(*d, entry.config.parameters)) return;

  const uint32_t intervalIdx = static_cast<uint32_t>(entry.expression->getTarget());
  const uint32_t nowMs = millis();
  if (recentEvents_.seen(entry.config.type, intervalIdx, nowMs)) return;
  recentEvents_.record(entry.config.type, intervalIdx, nowMs);

  ExpressionInvocation inv;
  inv.type = entry.config.type;
  inv.colors = entry.expression->getColors();
  inv.target = static_cast<uint8_t>(entry.expression->getTarget());
  inv.parameters = parametersWithoutCascadeKeys(entry.config.parameters);

  std::string json;
  serializeInvocation(inv, json);
  if (json.size() > lamp_protocol::EVENT_MAX_PAYLOAD) return;
  meshLink_->sendEvent(reinterpret_cast<const uint8_t*>(json.data()), json.size());
}

std::vector<AnimatedBehavior*> ExpressionManager::getBehaviors() {
  std::vector<AnimatedBehavior*> behaviors;
  for (auto& entry : expressions) {
    behaviors.push_back(entry.expression.get());
  }
  return behaviors;
}

void ExpressionManager::clear() {
  // Drop active-test pointers first; they point into the entries about
  // to be destroyed.
  activeTests_.clear();
  expressions.clear();
}

bool ExpressionManager::triggerExpression(const std::string& type) {
  bool triggered = false;
  const ExpressionEntry* firstFired = nullptr;
  // Suppress per-entry cascade callbacks from Expression::trigger();
  // a single cascade for the logical trigger is batched after the loop.
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
  // Cascade once per logical trigger. A TARGET_BOTH
  // expression has two entries (shade + base) but should fan out a single
  // invocation that receivers' own managers expand back to both sides.
  if (firstFired) { maybeCascade(*firstFired); emitEvent(*firstFired); }
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
#ifdef LAMP_DEBUG
  Serial.printf("[trigger] '%s' target=%d fired=%d\n",
                type.c_str(), static_cast<int>(target), triggered);
#endif
  if (firstFired) { maybeCascade(*firstFired); emitEvent(*firstFired); }
  return triggered;
}

ExpressionManager::ExpressionEntry
ExpressionManager::entryFromInvocation(const ExpressionInvocation& inv) {
  ExpressionTarget target = static_cast<ExpressionTarget>(inv.target);
  FrameBuffer* buffer = (target == TARGET_BASE) ? baseBuffer : shadeBuffer;

  ExpressionEntry entry;
  entry.type = inv.type;
  entry.config.type = inv.type;
  entry.config.colors = inv.colors;
  entry.config.target = inv.target;
  entry.config.parameters = inv.parameters;
  entry.config.setParameter(kParamCascadeEnabled, 1);
  entry.expression = makeExpression(inv.type, buffer, inv.colors,
                                    entry.config.intervalMin,
                                    entry.config.intervalMax,
                                    target, entry.config.parameters);
  return entry;
}

void ExpressionManager::broadcastInvocation(const ExpressionInvocation& inv,
                                            const uint8_t* excludeMac) {
  if (!shadeBuffer || !baseBuffer) return;

  ExpressionEntry entry = entryFromInvocation(inv);
  if (!entry.expression) return;

  maybeCascade(entry, excludeMac);
  emitEvent(entry);
}

void ExpressionManager::sendInvocationTo(const uint8_t mac[6],
                                         const ExpressionInvocation& inv) {
  if (!meshLink_) return;
  std::string json;
  serializeInvocation(inv, json);
  if (json.size() > lamp_protocol::COMMAND_MAX_PAYLOAD) return;
  meshLink_->sendCommand(mac, reinterpret_cast<const uint8_t*>(json.data()),
                         json.size());
}

void ExpressionManager::onExpressionFired(Expression* e) {
  if (suppressCascade_ || !e) return;
  for (auto& entry : expressions) {
    if (entry.expression.get() == e) {
#ifdef LAMP_DEBUG
      Serial.printf("[expr] fired type=%s target=%d\n", entry.type.c_str(),
                    static_cast<int>(e->getTarget()));
#endif
      maybeCascade(entry);
      emitEvent(entry);
      return;
    }
  }
}

bool ExpressionManager::triggerInvocation(const ExpressionInvocation& inv,
                                          const uint8_t srcMac[6], bool broadcast) {
  if (!shadeBuffer || !baseBuffer) return false;

  ExpressionTarget invTarget = static_cast<ExpressionTarget>(inv.target);

  // Coalesce: if a transient with the same (sender, type) is still
  // animating, drop this incoming cascade. Prevents pile-up from one
  // chatty sender (e.g. spam-tapping Test on the originator) while still
  // letting concurrent cascades from DIFFERENT senders both land; each
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

  // Home mode makes the per-type disable list authoritative for received
  // cascades too: drop a home-disabled type at receipt so no transient is
  // created (the render gate would refuse it, leaving a zombie until GC).
  if (compositor_ && compositor_->homeModeSkipsType(inv.type)) {
#ifdef LAMP_DEBUG
    Serial.printf("[expr] home-mode drop %s (type disabled)\n", inv.type.c_str());
#endif
    return false;
  }

  // Determine target buffers, same convention as addExpression. TARGET_BOTH
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
    // vector; the receiver's local config is intentionally irrelevant.
    // The cascade is a self-contained "execute this expression once and
    // forget it" command; the receiver's own configured expressions remain
    // entirely independent (untouched, unread, unmodified).
    auto expr = makeExpression(inv.type, buffer, inv.colors,
                               /*intervalMin*/ 60, /*intervalMax*/ 900,
                               invTarget, inv.parameters);
    if (!expr) {
#ifdef LAMP_DEBUG
      Serial.printf("[expr] transient rejected type=%s (unknown type)\n",
                    inv.type.c_str());
#endif
      continue;
    }
    expr->autoTriggerEnabled = false;  // transients fire once; the STOPPED-state auto-retrigger and any onComplete re-chain are gated off

    Expression* raw = expr.get();
    // addBehavior wires the BehaviorContext trigger() needs; a trigger the
    // wisp gate (or buffer routing) rejects never starts, and retaining the
    // dead STOPPED transient would block same-sender coalescing for the
    // full GC lifetime.
    if (compositor_) compositor_->addBehavior(raw);
    if (!raw->trigger()) {
      if (compositor_) compositor_->removeBehavior(raw);
#ifdef LAMP_DEBUG
      Serial.printf("[expr] transient rejected type=%s (never started)\n",
                    inv.type.c_str());
#endif
      continue;
    }
    TransientExpression t;
    t.type = inv.type;
    std::memcpy(t.srcMac, srcMac, 6);
    t.expression = std::move(expr);
    t.createdMs = millis();
    transientExpressions_.push_back(std::move(t));
    triggered = true;
#ifdef LAMP_DEBUG
    Serial.printf("[expr] transient created type=%s target=%d t=%lu\n",
                  inv.type.c_str(), static_cast<int>(invTarget),
                  (unsigned long)millis());
#endif
  }

  suppressCascade_ = false;

  // `broadcast` originates a wave from this call, distinct from suppressCascade_
  // which only stops the transient's own trigger() from re-firing one.
  if (triggered && broadcast) {
    ExpressionEntry entry = entryFromInvocation(inv);
    maybeCascade(entry);
    emitEvent(entry);
  }

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
      if (expired && !complete) {
        Serial.printf("[expr] WARN transient reaped expired type=%s age=%lums (never completed)\n",
                      it->type.c_str(),
                      (unsigned long)(nowMs - it->createdMs));
      }
#ifdef LAMP_DEBUG
      Serial.printf("[expr] transient reaped type=%s (%s) t=%lu\n",
                    it->type.c_str(), complete ? "complete" : "expired",
                    (unsigned long)nowMs);
#endif
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

}  // namespace lamp