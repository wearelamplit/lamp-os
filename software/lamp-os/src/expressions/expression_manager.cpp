#include "expression_manager.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

#include "expressions/param_utils.hpp"
#include "expressions/cascade_scheduler.hpp"
#include "expressions/surface_plan.hpp"
#include "util/heap_guard.hpp"
#include "util/uptime.hpp"

#include "components/network/protocol/lamp_protocol.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"

namespace lamp {

void ExpressionManager::begin(FrameBuffer* shade, FrameBuffer* base) {
  shadeBuffer = shade;
  baseBuffer = base;
  transientExpressions_.reserve(kMaxTransients);

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
    const ExpressionParams& parameters,
    FrameBuffer* above) {
  const ExpressionDescriptor* d = registry_.find(type.c_str());
  if (!d || !d->make) return nullptr;

  std::unique_ptr<Expression> expr(d->make(buffer));
  if (above) expr->joinSurfaceAbove(above);
  if (d->interval && d->interval->minGap) {
    intervalMax = clampRangeHiGap(intervalMin, intervalMax,
                                  static_cast<uint32_t>(d->interval->minGap),
                                  static_cast<uint32_t>(d->interval->max));
  }
  expr->configure(colors, intervalMin, intervalMax, target);

  ExpressionParams effective = parameters;
  registry_.applyDefaults(*d, effective,
                          (buffer && buffer->pixelCount > 0) ? buffer->pixelCount : 1);
  expr->configureFromParameters(effective);
  return expr;
}

const ExpressionParams& ExpressionManager::paramsOf(const ExpressionEntry& entry) const {
  static const ExpressionParams kNone;
  if (entry.ownParams) return *entry.ownParams;
  const ExpressionParams* p =
      configParams_ && entry.expression
          ? configParams_(entry.type, static_cast<uint8_t>(entry.expression->getTarget()))
          : nullptr;
  return p ? *p : kNone;
}

void ExpressionManager::addExpression(const ExpressionConfig& config, bool configBacked) {
  if (!shadeBuffer || !baseBuffer) return;

  auto target = static_cast<ExpressionTarget>(config.target);
  const SurfacePlan plan =
      surfacePlan(registry_.find(config.type.c_str()), target, shadeBuffer, baseBuffer);

  for (uint8_t b = 0; b < plan.count; ++b) {
    if (auto expr = makeExpression(config.type, plan.buffers[b], config.colors,
                                   config.intervalMin, config.intervalMax,
                                   target, config.parameters, plan.above)) {
      expr->autoTriggerEnabled = config.enabled;
      expressions.push_back(
          {std::move(expr), config.type,
           configBacked ? nullptr : std::make_unique<ExpressionParams>(config.parameters)});
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

static_assert(CascadeScheduler::kMaxPeers == LampRoster::kCapacity,
              "a cascade holds every peer the roster can");

void ExpressionManager::maybeCascade(const ExpressionEntry& entry,
                                     const uint8_t* excludeMac) {
  if (!entry.expression) return;
  cascade(shapeOf(entry), paramsOf(entry), excludeMac);
}

void ExpressionManager::cascade(const ExpressionInvocation& shape,
                                const ExpressionParams& params, const uint8_t* excludeMac) {
  if (!meshLink_) return;
  if (params.get(kParamCascadeEnabled, 0) == 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip not-enabled type=%s\n", shape.type.c_str());
#endif
    return;
  }

  // Continuous expressions retrigger at boot, settings upsert, and wisp
  // release; a stored cascadeEnabled=1 would fan out one-shots on each. The
  // app hides the cascade toggle for continuous types, so gate here to match.
  const ExpressionDescriptor* d = registry_.find(shape.type.c_str());
  if (!d) return;
  if (effectiveContinuous(*d, params)) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip continuous type=%s\n", shape.type.c_str());
#endif
    return;
  }

  const uint32_t intervalIdx = static_cast<uint32_t>(shape.target);
  const uint32_t nowMs = millis();
  if (recentCascades_.seen(shape.type, intervalIdx, nowMs)) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip dedup type=%s\n", shape.type.c_str());
#endif
    return;
  }
  recentCascades_.record(shape.type, intervalIdx, nowMs);

  const uint32_t staggerMs = params.get(kParamCascadeStaggerMs, 0);

  ExpressionInvocation inv;
  inv.type = shape.type;
  inv.colors = shape.colors;
  inv.target = shape.target;
  inv.parameters = parametersWithoutCascadeKeys(params);

  std::vector<MeshPeer>& targets = lampRoster.getMesh(LAMP_PRUNE_TIME_MS);
  uint8_t myMac[6];
  meshLink_->getMyMac(myMac);
  targets.erase(std::remove_if(targets.begin(), targets.end(),
                               [&](const MeshPeer& p) {
                                 return !p.hasMac ||
                                        std::memcmp(p.mac, myMac, 6) == 0 ||
                                        (excludeMac &&
                                         std::memcmp(p.mac, excludeMac, 6) == 0);
                               }),
                targets.end());
  std::sort(targets.begin(), targets.end(),
            [](const MeshPeer& a, const MeshPeer& b) {
              return a.lastRssi > b.lastRssi;
            });

  if (targets.empty()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip no-peers type=%s\n", shape.type.c_str());
#endif
    return;
  }
  if (meshLink_->isOtaInProgress()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] skip ota type=%s\n", shape.type.c_str());
#endif
    return;
  }

  // Every queued ESP-NOW frame holds a WiFi TX buffer until it airs, so the
  // fan-out drains a batch at a time from fixed storage (tickCascade). It
  // waits behind a cascade still draining; with both slots taken it
  // replaces the one waiting.
  std::string json;
  serializeInvocation(inv, json);
  const CascadeScheduler::Begin queued =
      cascade_.begin(json.data(), json.size(), staggerMs, nowMs);
  if (queued == CascadeScheduler::Begin::Replaced) {
    Serial.printf("[cascade] type=%s replaced a waiting cascade\n", shape.type.c_str());
  }
  // Unconditional: a refused cascade silently fails on deployed lamps; this
  // is the only signal. Each line stays under Print's 64-byte stack buffer
  // with the longest type id, so logging it takes no heap.
  if (queued == CascadeScheduler::Begin::TooLong) {
    Serial.printf("[cascade] ERR %uB > %uB, dropped %s\n", (unsigned)json.size(),
                  (unsigned)CascadeScheduler::kMaxPayloadLen, shape.type.c_str());
    return;
  }
  if (queued == CascadeScheduler::Begin::NoDelayField) {
    Serial.printf("[cascade] ERR no delayMs, dropped %s\n", shape.type.c_str());
    return;
  }
  for (const MeshPeer& peer : targets) cascade_.addPeer(peer.mac);
#ifdef LAMP_DEBUG
  Serial.printf("[cascade] start type=%s peers=%u\n", shape.type.c_str(),
                (unsigned)cascade_.pending());
#endif
  tickCascade(nowMs);
}

void ExpressionManager::retireStale(uint32_t nowMs) {
  recentCascades_.retire(nowMs);
  recentEvents_.retire(nowMs);
  localCooldown_.retire(nowMs);
  cascade_.retireBatchGap(nowMs);
  recentPlays_.retire(nowMs);
}

bool ExpressionManager::playedWithin(const char* type, uint32_t windowMs, uint32_t nowMs) const {
  return recentPlays_.playedWithin(registry_.find(type), windowMs, nowMs);
}

void ExpressionManager::tickCascade(uint32_t nowMs) {
  if (!cascade_.active() || !meshLink_) return;
  if (meshLink_->isOtaInProgress()) {
#ifdef LAMP_DEBUG
    Serial.printf("[cascade] drain stopped for OTA, %u peers left\n",
                  (unsigned)cascade_.pending());
#endif
    cascade_.cancel();
    return;
  }
  const uint32_t overflowBefore = cascade_.overflowDrops();
  // A frame the driver has no room for waits for a later pass.
  cascade_.tickGated(
      nowMs, [this, nowMs] { return meshLink_->txReady(nowMs); },
      [this](const uint8_t* mac, const char* payload, size_t len) {
        meshLink_->sendCommand(mac, reinterpret_cast<const uint8_t*>(payload), len);
      });
  if (cascade_.overflowDrops() != overflowBefore) {
    Serial.printf("[cascade] ERR %u frames dropped: delay rewrite over %uB\n",
                  (unsigned)(cascade_.overflowDrops() - overflowBefore),
                  (unsigned)CascadeScheduler::kMaxPayloadLen);
  }
}

void ExpressionManager::emitEvent(const ExpressionEntry& entry) {
  if (!entry.expression) return;
  recentPlays_.record(registry_.find(entry.type.c_str()), millis());
  announce(shapeOf(entry), paramsOf(entry));
}

void ExpressionManager::announce(const ExpressionInvocation& shape,
                                 const ExpressionParams& params) {
  if (!meshLink_) return;
  if (meshLink_->isOtaInProgress()) return;

  // Continuous expressions retrigger at boot, settings upsert, and wisp
  // release; announcing those would spuriously trigger peer mirrors.
  const ExpressionDescriptor* d = registry_.find(shape.type.c_str());
  if (!d || effectiveContinuous(*d, params)) return;

  const uint32_t intervalIdx = static_cast<uint32_t>(shape.target);
  const uint32_t nowMs = millis();
  if (recentEvents_.seen(shape.type, intervalIdx, nowMs)) return;
  recentEvents_.record(shape.type, intervalIdx, nowMs);

  ExpressionInvocation inv;
  inv.type = shape.type;
  inv.colors = shape.colors;
  inv.target = shape.target;
  inv.parameters = parametersWithoutCascadeKeys(params);
  inv.hops = shape.hops;

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

ExpressionInvocation ExpressionManager::shapeOf(const ExpressionEntry& entry) {
  ExpressionInvocation shape;
  shape.type = entry.type;
  shape.colors = entry.expression->getColors();
  shape.target = static_cast<uint8_t>(entry.expression->getTarget());
  return shape;
}

void ExpressionManager::broadcastFrom(const ExpressionInvocation& inv,
                                      const uint8_t* excludeMac) {
  ExpressionParams params = inv.parameters;
  params.set(kParamCascadeEnabled, 1);
  cascade(inv, params, excludeMac);
  announce(inv, params);
}

void ExpressionManager::broadcastInvocation(const ExpressionInvocation& inv,
                                            const uint8_t* excludeMac) {
  if (!shadeBuffer || !baseBuffer) return;

  broadcastFrom(inv, excludeMac);
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
                                          const uint8_t srcMac[6], bool broadcast,
                                          TransientSource source) {
  if (!shadeBuffer || !baseBuffer) return false;

  if (compositor_ && compositor_->blocksRemote(source)) return false;

  ExpressionTarget invTarget = static_cast<ExpressionTarget>(inv.target);

  // Coalesce: if a transient with the same (sender, type) is still
  // animating, drop this incoming cascade. Prevents pile-up from one
  // chatty sender (e.g. spam-tapping Test on the originator) while still
  // letting concurrent cascades from DIFFERENT senders both land; each
  // sender contributes one in-flight transient. A retiring transient is on its
  // way out and does not hold the sender's place.
  for (const auto& t : transientExpressions_) {
    if (t.type == inv.type && t.expression &&
        std::memcmp(t.srcMac, srcMac, 6) == 0 &&
        !t.expression->isAnimationComplete() && !t.expression->retiring()) {
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

  const ExpressionDescriptor* descriptor = registry_.find(inv.type.c_str());
  const SurfacePlan plan = surfacePlan(descriptor, invTarget, shadeBuffer, baseBuffer);
  const size_t targetCount = plan.count;
  if (targetCount == 0) return false;

  const uint32_t nowMs = millis();
  if (source == TransientSource::Local && localCooldown_.waiting(inv.type.c_str(), nowMs)) {
    return false;
  }
  // A crowd of distinct senders would otherwise stack one transient pair per
  // sender, each a heap Expression plus its buffers.
  const bool admitted = admitTransientAmong(transientExpressions_, targetCount, source,
                                            heapCanSpare(kTransientHeapBytes * targetCount));
  if (!admitted) {
    if (source == TransientSource::Local) localCooldown_.refused(inv.type.c_str(), nowMs);
    static uint64_t s_lastDropLogMs = 0;
    const uint64_t now = uptimeMs();
    if (s_lastDropLogMs == 0 || now - s_lastDropLogMs >= kTransientDropLogMs) {
      s_lastDropLogMs = now;
      Serial.printf("[expr] transient drop %.16s (%s, %u in flight)\n", inv.type.c_str(),
                    source == TransientSource::Local ? "local" : "remote",
                    (unsigned)transientExpressions_.size());
    }
  }

  // Loop-break invariant: the transient's trigger() will call
  // onExpressionFired via the global manager pointer; suppress so a
  // remote-received trigger can never re-cascade. Receivers are terminal in
  // the propagation graph.
  suppressCascade_ = true;

  bool triggered = false;
  for (size_t b = 0; admitted && b < targetCount; ++b) {
    FrameBuffer* buffer = plan.buffers[b];
    // Build a fresh one-shot Expression instance directly from the
    // invocation. NEVER consults this lamp's `expressions` (configured)
    // vector; the receiver's local config is intentionally irrelevant.
    // The cascade is a self-contained "execute this expression once and
    // forget it" command; the receiver's own configured expressions remain
    // entirely independent (untouched, unread, unmodified).
    auto expr = makeExpression(inv.type, buffer, inv.colors,
                               /*intervalMin*/ 60, /*intervalMax*/ 900,
                               invTarget, inv.parameters, plan.above);
    if (!expr) {
#ifdef LAMP_DEBUG
      Serial.printf("[expr] transient rejected type=%.16s (unknown type)\n",
                    inv.type.c_str());
#endif
      continue;
    }
    expr->isTransient = true;
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
    t.source = source;
    transientExpressions_.push_back(std::move(t));
    triggered = true;
#ifdef LAMP_DEBUG
    Serial.printf("[expr] transient created type=%s target=%d t=%lu\n",
                  inv.type.c_str(), static_cast<int>(invTarget),
                  (unsigned long)millis());
#endif
  }

  suppressCascade_ = false;
  if (triggered) recentPlays_.record(descriptor, nowMs);

  // `broadcast` originates a wave from this call, distinct from suppressCascade_
  // which only stops the transient's own trigger() from re-firing one.
  if (cascadesFrom(broadcast, triggered, source)) broadcastFrom(inv);

  return triggered;
}

bool ExpressionManager::ignoresPeers() const {
  return compositor_ && compositor_->blocksRemote(TransientSource::Remote);
}

bool ExpressionManager::inHomeMode() const {
  return compositor_ && compositor_->homeMode;
}

bool ExpressionManager::replayAndAnnounce(const ExpressionInvocation& inv,
                                          const uint8_t srcMac[6], TransientSource source) {
  if (!triggerInvocation(inv, srcMac, /*broadcast=*/false, source)) return false;
  announce(inv, inv.parameters);
  return true;
}

// Evict a one-shot cascade even when isAnimationComplete() never fires; a
// stuck completion signal would otherwise latch it on the compositor forever.
static constexpr uint32_t kTransientMaxLifetimeMs = 180000;

void ExpressionManager::gcTransients() {
  if (transientExpressions_.empty()) return;
  const uint32_t nowMs = millis();
  for (auto it = transientExpressions_.begin(); it != transientExpressions_.end();) {
    if (it->expression && compositor_ && compositor_->discardsRemote(it->source)) {
      it->expression->retire();
    }
    // A transient home mode skips by type would freeze mid-animation and
    // resume when home mode ends; while home mode discards peers it is
    // reaped instead.
    const bool skippedAtHome = compositor_ &&
                               compositor_->discardsRemote(TransientSource::Remote) &&
                               compositor_->homeModeSkipsType(it->type);
    const bool complete = skippedAtHome ||
                          (it->expression && (it->expression->isAnimationComplete() ||
                                              it->expression->retired()));
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

void ExpressionManager::upsertExpression(const ExpressionConfig& config, Compositor* compositor,
                                         bool configBacked) {
  ExpressionTarget target = static_cast<ExpressionTarget>(config.target);
  removeExpression(config.type, target, compositor);

  size_t prevCount = expressions.size();
  addExpression(config, configBacked);

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