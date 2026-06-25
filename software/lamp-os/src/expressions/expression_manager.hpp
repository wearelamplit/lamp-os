#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "config/config_types.hpp"
#include "core/frame_buffer.hpp"
#include "expression.hpp"
#include "expression_invocation.hpp"
#include "glitchy_expression.hpp"
#include "shifty_expression.hpp"
#include "pulse_expression.hpp"
#include "breathing_expression.hpp"

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
class ShowReceiver;

// Forwarder implemented in lamp.cpp. tryHandleExpressionEvent
// uses this to push a delayed invocation into the loop-task pendingTriggers
// queue when suppliedDelayMs > 0; immediate fires go straight to
// triggerInvocation. Keeps the queue's storage in standard_lamp where the
// drain lives — the manager doesn't need to own a second queue.
void enqueueDelayedInvocation(const ExpressionInvocation& inv,
                              const uint8_t srcMac[6],
                              uint32_t delayMs);

/**
 * @brief Manages active expressions and their lifecycle
 */
class ExpressionManager {
 private:
  // Store expression with its type for triggering. `config` is a snapshot
  // of the ExpressionConfig that built this entry — kept so the manager can
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
  ShowReceiver* showReceiver_ = nullptr;
  Compositor* compositor_ = nullptr;
  // Set during the manager's own trigger* loops so per-entry Expression::trigger()
  // callbacks don't fan out a cascade we're already handling explicitly (or
  // intentionally skipping for remote-arrived invocations). Loop-task only —
  // no concurrency.
  bool suppressCascade_ = false;

  // One-shot Expression instances created on-demand by triggerInvocation when
  // a remote cascade arrives. They live in the compositor for the duration of
  // their animation, then gcTransients() removes them. Entirely independent of
  // the `expressions` (configured) vector — no interaction with the receiver's
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
  // then the base entry fires microseconds later — recentCascades_.seen()
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

  // Send the cascade fan-out for an expression that just fired locally,
  // if its config opts in via the cascadeEnabled parameter. No-op when
  // no ShowReceiver has been wired in. Never called for remote-arrived
  // triggers — that's the structural loop break.
  //
  // Internally gates on recentCascades_ to enforce the "cascade once per
  // logical trigger" invariant: a TARGET_BOTH expression auto-firing the
  // shade and base entries in the same tick must produce exactly one
  // outbound cascade. Use the (type, intervalIdx) key — currently the
  // entry's target field, which is identical across both halves of a
  // TARGET_BOTH config and distinct for TARGET_SHADE vs TARGET_BASE.
  void maybeCascade(const ExpressionEntry& entry);

 public:
  /**
   * @brief Initialize manager with frame buffers
   */
  void begin(FrameBuffer* shade, FrameBuffer* base);

  /**
   * @brief Wire up the mesh send path for the cascade convention. Optional —
   *        when unset, cascade is silently disabled (boot before mesh ready,
   *        or test environments).
   */
  void setShowReceiver(ShowReceiver* receiver);

  /**
   * @brief Wire up the compositor so triggerInvocation can register transient
   *        one-shot Expressions built from incoming remote invocations.
   *        Without this, remote cascades are silently no-op'd.
   */
  void setCompositor(Compositor* compositor);

  /**
   * @brief Load expressions from config
   */
  void loadFromConfig(const ExpressionSettings& settings);

  /**
   * @brief Get active expression behaviors for compositor
   */
  std::vector<AnimatedBehavior*> getBehaviors();

  /**
   * @brief Add a new expression. Used at boot by loadFromConfig (compositor
   *        not yet built). Runtime callers should use upsertExpression.
   */
  void addExpression(const ExpressionConfig& config);

  /**
   * @brief Clear all expressions. Only safe before the compositor has been
   *        built — does not unregister behaviors from a running compositor.
   */
  void clear();

  /**
   * @brief Trigger every expression whose type matches. LOCAL path —
   *        honors the cascade convention if configured.
   */
  bool triggerExpression(const std::string& type);

  /**
   * @brief Trigger expressions matching both type and target. Used by the
   *        per-row Test button to fire exactly the configured instance.
   *        LOCAL path — honors the cascade convention if configured.
   */
  bool triggerExpression(const std::string& type, ExpressionTarget target);

  /**
   * @brief REMOTE path. Called only by the receive side of the mesh when a
   *        triggerExpression CONTROL_OP arrives. Builds a fresh transient
   *        Expression instance from the invocation's colors + params and
   *        fires it once; the receiver's own configured expressions are
   *        not consulted. Transients live in the compositor for the
   *        duration of their animation and are reaped by gcTransients().
   *        NEVER cascades — this is the structural loop break that makes
   *        flood propagation safe.
   */
  bool triggerInvocation(const ExpressionInvocation& inv,
                         const uint8_t srcMac[6]);

  /**
   * @brief MSG_EVENT receive-side handler. Called from the Core 1 drain
   *        for an ExpressionTriggered event. Does a cheap JSON peek for
   *        `"type":"..."` first so an unconfigured-cascade-type event can
   *        be dropped without paying the full JsonDocument parse. Then
   *        checks the local cascadeEnabled config + RecentCascade dedup
   *        before the full parseInvocation + triggerInvocation with
   *        fireAtMs = millis() + suppliedDelayMs.
   *
   *        suppliedDelayMs is the stagger value the recv-side already
   *        looked up from the MSG_EVENT staggerEntries list (own MAC →
   *        delayMs, or tail-fire fallback if absent). Clamped to
   *        kMaxDelayMs defensively here so an attacker who builds a
   *        stagger entry with a huge delay can't hold a pendingTriggers
   *        slot for ~49 days.
   */
  void tryHandleExpressionEvent(const uint8_t sourceMac[6],
                                uint16_t suppliedDelayMs,
                                const uint8_t* payload, uint16_t payloadLen);

  /**
   * @brief Called by Expression::trigger() after onTrigger() runs, regardless
   *        of how trigger() was reached (auto-interval from control(),
   *        internal chain triggers, etc.). Looks up the entry by pointer and
   *        runs the cascade convention. Suppressed when the manager's own
   *        trigger* loops are running so they can batch cascade themselves.
   */
  void onExpressionFired(Expression* e);

  /**
   * @brief Garbage-collect transient one-shot expressions created by
   *        triggerInvocation whose animation has finished. Unregisters them
   *        from the compositor and destroys the instance. Cheap; safe to
   *        call every loop tick. Call AFTER compositor.tick() so the final
   *        frame of the animation gets drawn before removal.
   */
  void gcTransients();

  std::vector<Color> getExpressionColors(const std::string& type) const;

  /**
   * @brief Live insert-or-update keyed by (type, target). Destroys any
   *        existing entries for (type, target), builds fresh ones from
   *        config, and registers them with the compositor.
   */
  void upsertExpression(const ExpressionConfig& config, Compositor* compositor);

  /**
   * @brief Live remove keyed by (type, target). Unregisters from compositor
   *        first, then destroys the Expression instances.
   */
  void removeExpression(const std::string& type, ExpressionTarget target, Compositor* compositor);

  /**
   * @brief Register every expression entry matching (type, target) as
   *        currently being previewed by the app's Test button. Called from
   *        the test_expression dispatch path immediately after
   *        triggerExpression(...). Returns true iff the active-test set
   *        transitioned from empty to non-empty (caller emits the
   *        previewActive=true state-notify edge).
   */
  bool markTestActive(const std::string& type, ExpressionTarget target);

  /**
   * @brief Drop every active-test entry whose underlying Expression has
   *        returned to STOPPED. Called from the loop tick (same cadence as
   *        gcTransients). Returns true iff the set transitioned from
   *        non-empty to empty (caller emits the previewActive=false edge).
   */
  bool reapCompletedTests();

  /**
   * @brief Clear all active-test entries. Called from the
   *        test_expression_complete dispatch path. Returns true iff the set
   *        transitioned from non-empty to empty.
   */
  bool clearAllTestActive();

  /**
   * @brief True iff at least one expression is currently being previewed
   *        via the app's Test button. Read by ble_control::notifyStateChange
   *        to populate the previewActive bit.
   */
  bool isAnyTestActive() const { return !activeTests_.empty(); }
};

}  // namespace lamp
