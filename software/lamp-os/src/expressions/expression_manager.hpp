#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config/config_types.hpp"
#include "core/frame_buffer.hpp"
#include "expression.hpp"
#include "expression_invocation.hpp"
#include "expression_registry.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/breathing/breathing_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"

namespace lamp {

// Cascade dedup window. The TARGET_BOTH auto-trigger from Expression::control()
// fires shade+base entries back-to-back in the same loop tick (microseconds
// apart), so anything wider than a frame would catch the double-fire; 250 ms
// also tolerates a generous control()-iteration slip without being so loose
// it suppresses a deliberate back-to-back manual trigger. Pinned by
// test/test_cascade_dedup/cascade_dedup.cpp.
static constexpr uint32_t kCascadeDedupWindowMs = 250;

class Compositor;
class ExpressionManager;
class MeshLink;

// Forwarder implemented in lamp.cpp. Schedules a future triggerInvocation
// without each call site having to know the queue storage lives there.
// Runs on Core 1 (drain task); pendingTriggers is loop-task-only.
void enqueueDelayedInvocation(const ExpressionInvocation& inv,
                              const uint8_t srcMac[6],
                              uint32_t delayMs);

/**
 * Manages active expressions and their lifecycle.
 */
class ExpressionManager {
 private:
  // Store expression with its type for triggering. `config` is a snapshot
  // of the ExpressionConfig that built this entry, kept so the manager can
  // make cascade decisions without each Expression subclass having to
  // expose its raw parameter map.
  struct ExpressionEntry {
    std::unique_ptr<Expression> expression;
    std::string type;
    ExpressionConfig config;
  };
  std::vector<ExpressionEntry> expressions;
  FrameBuffer* shadeBuffer = nullptr;
  FrameBuffer* baseBuffer = nullptr;
  MeshLink* meshLink_ = nullptr;
  Compositor* compositor_ = nullptr;
  // Set during the manager's own trigger* loops so per-entry Expression::trigger()
  // callbacks don't fan out a cascade already being handled explicitly (or
  // intentionally skipping for remote-arrived invocations). Loop-task only,
  // no concurrency.
  bool suppressCascade_ = false;

  // One-shot Expression instances created on-demand by triggerInvocation when
  // a remote cascade arrives. They live in the compositor for the duration of
  // their animation, then gcTransients() removes them. Entirely independent of
  // the `expressions` (configured) vector; no interaction with the receiver's
  // local config in any direction.
  //
  // `type` + `srcMac` together key the coalesce check in triggerInvocation:
  // a new cascade with the same (type, srcMac) as an in-flight transient is
  // dropped (prevents pile-up from a chatty sender), while a different sender
  // or different type still fires.
  struct TransientExpression {
    std::string type;
    uint8_t srcMac[6];
    std::unique_ptr<Expression> expression;
    uint32_t createdMs = 0;
  };
  std::vector<TransientExpression> transientExpressions_;

  // Non-owning pointers to ExpressionEntry::expression instances that the
  // app's Test button fired via dispatchLampAction("test_expression").
  // Maintained by markTestActive() (called from the test_expression handler)
  // and reaped per-entry by reapCompletedTests() when their animationState
  // returns to STOPPED. Cleared en masse by clearAllTestActive() on
  // test_expression_complete. The set is the truth source for the
  // previewActive bit broadcast on CHAR_STATE_NOTIFY.
  std::vector<Expression*> activeTests_;

  // Small ring of (type, intervalIdx, fireMs) for cascades that have
  // already fanned out, so a TARGET_BOTH expression's per-entry auto-
  // trigger from Expression::control() doesn't double-cascade through
  // onExpressionFired(). The shade entry fires, records (type, idx, now),
  // then the base entry fires microseconds later; recentCascades_.seen()
  // returns true and maybeCascade() short-circuits. The pure data shape
  // (keying, eviction, window check) is mirrored in
  // test/test_cascade_dedup/cascade_dedup.cpp.
  //
  // CAPACITY=8 is generous: a logical trigger emits at most two entries
  // (TARGET_BOTH), and entries age out of the window (250 ms) inside a
  // few control() iterations.
  struct RecentCascade {
    static constexpr size_t CAPACITY = 8;

    bool seen(const std::string& type, uint32_t intervalIdx,
              uint32_t nowMs) const {
      for (size_t i = 0; i < CAPACITY; ++i) {
        const Entry& e = entries[i];
        if (!e.used) continue;
        if (e.type != type || e.intervalIdx != intervalIdx) continue;
        if (nowMs - e.fireMs <= kCascadeDedupWindowMs) return true;
      }
      return false;
    }

    void record(const std::string& type, uint32_t intervalIdx,
                uint32_t nowMs) {
      Entry& slot = entries[head];
      slot.used = true;
      slot.type = type;
      slot.intervalIdx = intervalIdx;
      slot.fireMs = nowMs;
      head = (head + 1) % CAPACITY;
    }

   private:
    struct Entry {
      bool used = false;
      std::string type;
      uint32_t intervalIdx = 0;
      uint32_t fireMs = 0;
    };
    Entry entries[CAPACITY];
    size_t head = 0;
  };
  RecentCascade recentCascades_;

  // Parallel dedup ring for MSG_EVENT announces. Same keying and eviction
  // policy as recentCascades_: a TARGET_BOTH expression's shade and base
  // entries auto-fire microseconds apart; recentEvents_.seen() suppresses
  // the second announce so observers receive exactly one per logical trigger.
  RecentCascade recentEvents_;

  // Fan out MSG_COMMAND to each nearby lamp for an expression that just
  // fired locally, if its config opts in via cascadeEnabled. Continuous
  // descriptors never cascade. No-op when no MeshLink has been wired in.
  // Never called for remote-arrived triggers; that's the structural loop
  // break.
  //
  // Internally gates on recentCascades_ to enforce the "cascade once per
  // logical trigger" invariant: a TARGET_BOTH expression auto-firing the
  // shade and base entries in the same tick must produce exactly one
  // outbound cascade. Use the (type, intervalIdx) key, currently the
  // entry's target field, which is identical across both halves of a
  // TARGET_BOTH config and distinct for TARGET_SHADE vs TARGET_BASE.
  //
  // `excludeMac`: if non-null, that peer is skipped in the target loop.
  void maybeCascade(const ExpressionEntry& entry,
                    const uint8_t* excludeMac = nullptr);

  // Broadcast a MSG_EVENT announce for a locally-fired expression. Gated on
  // recentEvents_ so a TARGET_BOTH auto-trigger produces exactly one event.
  void emitEvent(const ExpressionEntry& entry);

  // Synthesize a cascade-enabled ExpressionEntry from an invocation, selecting
  // the base/shade buffer by `inv.target`. Feeds maybeCascade + emitEvent.
  ExpressionEntry entryFromInvocation(const ExpressionInvocation& inv);

  ExpressionRegistry registry_;

  // Build an Expression bound to `buffer` for `type` via the registry's
  // descriptor. Folds descriptor defaults into `parameters` before
  // configureFromParameters. Returns nullptr for a type absent from the
  // registry. Shared by addExpression + triggerInvocation.
  std::unique_ptr<Expression> makeExpression(
      const std::string& type, FrameBuffer* buffer,
      const std::vector<Color>& colors,
      uint32_t intervalMin, uint32_t intervalMax,
      ExpressionTarget target,
      const std::map<std::string, uint32_t>& parameters);

 public:
  ExpressionRegistry& registry() { return registry_; }

  /**
   * Initialize manager with frame buffers.
   */
  void begin(FrameBuffer* shade, FrameBuffer* base);

  /**
   * Wire up the mesh send path for the cascade convention. Optional;
   * when unset, cascade is silently disabled (boot before mesh ready,
   * or test environments).
   */
  void setMeshLink(MeshLink* link);

  /**
   * Wire up the compositor so triggerInvocation can register transient
   * one-shot Expressions built from incoming remote invocations.
   * Without this, remote cascades are silently no-op'd.
   */
  void setCompositor(Compositor* compositor);

  /**
   * Load expressions from config.
   */
  void loadFromConfig(const ExpressionSettings& settings);

  /**
   * Get active expression behaviors for compositor.
   */
  std::vector<AnimatedBehavior*> getBehaviors();

  /**
   * Add a new expression. Used at boot by loadFromConfig (compositor
   * not yet built). Runtime callers should use upsertExpression.
   */
  void addExpression(const ExpressionConfig& config);

  /**
   * Clear all expressions. Only safe before the compositor has been
   * built; does not unregister behaviors from a running compositor.
   */
  void clear();

  /**
   * Trigger every expression whose type matches. LOCAL path;
   * honors the cascade convention if configured.
   */
  bool triggerExpression(const std::string& type);

  /**
   * Trigger expressions matching both type and target. Used by the
   * per-row Test button to fire exactly the configured instance.
   * LOCAL path; honors the cascade convention if configured.
   */
  bool triggerExpression(const std::string& type, ExpressionTarget target);

  /**
   * Builds a fresh transient Expression from the invocation's colors +
   * params and fires it once; the lamp's own configured expressions are
   * not consulted. Serves the mesh receive/forward paths and local
   * originations (the app Test button). Transients live in the compositor
   * for their animation and are reaped by gcTransients().
   *
   * The transient's own trigger() never re-cascades; that structural loop
   * break makes flood propagation safe. `broadcast` is a separate axis: true
   * only for a local origination (the app Test button) that must fan a wave
   * out, false for every receive/forward path so received cascades stay
   * terminal.
   */
  bool triggerInvocation(const ExpressionInvocation& inv,
                         const uint8_t srcMac[6], bool broadcast = false);

  /**
   * Broadcast an invocation to the crowd WITHOUT firing it locally.
   * Synthesizes a cascading entry from `inv` and runs the same
   * maybeCascade (directed RSSI-staggered wave) + emitEvent (announce)
   * path a locally-fired cascading expression uses, minus the local
   * render. snafu's greeting uses it to fan a crowd glitch on arrival
   * while its own scramble renders locally.
   *
   * `excludeMac`: if non-null, that peer is skipped in the target loop.
   * Default nullptr sends to all reachable peers.
   */
  void broadcastInvocation(const ExpressionInvocation& inv,
                           const uint8_t* excludeMac = nullptr);

  /**
   * Send an invocation directly to a single peer by MAC. Serializes
   * `inv`, size-guards against COMMAND_MAX_PAYLOAD, and calls
   * meshLink_->sendCommand. No-op if no meshLink_ is wired.
   */
  void sendInvocationTo(const uint8_t mac[6], const ExpressionInvocation& inv);

  /**
   * Called by Expression::trigger() after onTrigger() runs, regardless
   * of how trigger() was reached (auto-interval from control(),
   * internal chain triggers, etc.). Looks up the entry by pointer and
   * runs the cascade convention. Suppressed when the manager's own
   * trigger* loops are running so they can batch cascade themselves.
   */
  void onExpressionFired(Expression* e);

  /**
   * Garbage-collect transient one-shot expressions created by
   * triggerInvocation whose animation has finished. Unregisters them
   * from the compositor and destroys the instance. Cheap; safe to
   * call every loop tick. Call AFTER compositor.tick() so the final
   * frame of the animation gets drawn before removal.
   */
  void gcTransients();

  std::vector<Color> getExpressionColors(const std::string& type) const;

  /**
   * Live insert-or-update keyed by (type, target). Destroys any
   * existing entries for (type, target), builds fresh ones from
   * config, and registers them with the compositor.
   */
  void upsertExpression(const ExpressionConfig& config, Compositor* compositor);

  /**
   * Live remove keyed by (type, target). Unregisters from compositor
   * first, then destroys the Expression instances.
   */
  void removeExpression(const std::string& type, ExpressionTarget target, Compositor* compositor);

  /**
   * Register every expression entry matching (type, target) as
   * currently being previewed by the app's Test button. Called from
   * the test_expression dispatch path immediately after
   * triggerExpression(...). Returns true iff the active-test set
   * transitioned from empty to non-empty (caller emits the
   * previewActive=true state-notify edge).
   */
  bool markTestActive(const std::string& type, ExpressionTarget target);

  /**
   * Drop every active-test entry whose underlying Expression has
   * returned to STOPPED. Called from the loop tick (same cadence as
   * gcTransients). Returns true iff the set transitioned from
   * non-empty to empty (caller emits the previewActive=false edge).
   */
  bool reapCompletedTests();

  /**
   * Clear all active-test entries. Called from the
   * test_expression_complete dispatch path. Returns true iff the set
   * transitioned from non-empty to empty.
   */
  bool clearAllTestActive();

  /**
   * True iff at least one expression is currently being previewed
   * via the app's Test button. Read by ble_control::notifyStateChange
   * to populate the previewActive bit.
   */
  bool isAnyTestActive() const { return !activeTests_.empty(); }
};

// Serialized expression catalog. Immutable after boot registration, so it's
// built once on first call and cached for the process lifetime.
const std::string& expressionCatalogJson();

}  // namespace lamp
