#pragma once

#include <cstring>
#include <string>
#include <vector>

#include <lampos/clock.hpp>

#include "animated_behavior.hpp"
#include "components/network/mesh/pending_slots.hpp"
#include "behavior_context.hpp"
#include "frame_buffer.hpp"
#include "home_mode_gate.hpp"
#include "render/layer_stack.hpp"

#define MINIMUM_FRAME_DRAW_TIME_MS 16
#define STARTUP_ANIMATION_FRAMES 120
#define REBOOT_ANIMATION_FRAMES 120

namespace lamp {
/**
 * The compositor singleton coordinates a list of behaviors in the order
 * they need to draw.
 */
class Compositor {
 private:
  // Expression-band bounds in `behaviors`, [start, end). Set by the lamp
  // after pushing the initial stack; runtime adds/removes maintain them.
  // addBehavior inserts at `end` (new expressions join the band top);
  // addBaseBehavior inserts at `start` (base-scene behaviours draw below
  // the whole band). Everything below `start` (configurators, base scenes)
  // draws first; expressions compose on top.
  size_t expressionBandStart = 0;
  size_t expressionBandEnd = 0;

  // The shared per-behavior context. Owned here; every registered behavior
  // gets a pointer via setBehaviorContext. The lamp wires the ExpressionManager
  // pointer and frame-buffer list in initBehaviors(). Single instance, multiple readers.
  BehaviorContext context_;

  // Home-mode suppression policy. Updated via setHomeModePolicy().
  bool homeSocialDisabled_ = true;
  std::vector<std::string> homeDisabledExprIds_ = {"glitchy"};

  // true once a quiet-mode frame has painted; the next false->true edge on
  // ota_quiet_mode::isQuiet() signals ota_indicator::paint() to (re)init its
  // base-surface settle.
  bool otaQuietPainted_ = false;

  // Wisp color layer, one stack per surface, composited over the base scene
  // (configurator/idle) and beneath the overlay behaviors so expressions +
  // greeting still render on top. Both surfaces share a single MSG_WISP_STATE
  // presence (the wisp always paints base+shade together): the [0,1] reveal
  // eases toward 1 while STATE holds this lamp, 0 otherwise.
  LayerStack wispShadeStack_;
  LayerStack wispBaseStack_;
  EasedScalar wispShadePresence_;
  EasedScalar wispBasePresence_;

  // MSG_WISP_STATE render + indicator state. present_ tracks whether the last
  // fresh STATE from this lamp's wisp painted it; freshMs_ ages present out as
  // a failsafe when STATE stops arriving. base/shade drive the app indicator.
  bool     wispStatePresent_ = false;
  uint32_t wispStateFreshMs_ = 0;
  Color    wispStateBase_;
  Color    wispStateShade_;

  Color    wispStateHeadingBase_;
  Color    wispStateHeadingShade_;
  uint8_t  wispStateSource_[6] = {};
  WispFadeSync wispFadeSync_ = WispFadeSync::Kept;

  // Overlay the wisp layer onto frameBuffers[0]=shade / [1]=base in place.
  void compositeWisp();

  // One surface's share of a STATE frame that paints this lamp, `plain` being
  // its colour in the frame. A record runs the timed fade, joining from
  // `plain` while the layer is hidden. A partial frame without one leaves a
  // running fade be when `continuing`. Otherwise the layer eases to `plain`,
  // edge-triggered on `plainMoved`, ending any fade.
  static WispFadeSync feedWispSurface(LayerStack& s, float presence, Color plain,
                                      bool plainMoved, bool continuing,
                                      lamp_protocol::WispDriftFadeLookup fade,
                                      Color target, uint32_t leftMs,
                                      uint32_t nowMs) {
    using Lookup = lamp_protocol::WispDriftFadeLookup;
    if (fade == Lookup::Record) {
      if (presence == 0.0f) s.snapWisp(plain);
      return s.syncWispFade(target, leftMs, nowMs);
    }
    if (fade == Lookup::Partial && continuing && s.wispFadeRunning(nowMs)) {
      return WispFadeSync::Kept;
    }
    if (plainMoved || s.wispTimed()) s.setWispTarget(plain);
    return WispFadeSync::Kept;
  }

 public:
  // This frame's eased wisp presence for a surface, in [0,1]. Drives the
  // wisp composite; expressions read the same value so their wisp-dim eases
  // in lockstep with the wisp fade (no separate crossfade). compositeWisp()
  // ticks it before expressions draw, so it is current when they query.
  float wispPresence(bool base) const {
    return base ? wispBasePresence_.value() : wispShadePresence_.value();
  }

  std::vector<AnimatedBehavior*> underlayBehaviors;
  std::vector<AnimatedBehavior*> startupBehaviors;
  std::vector<AnimatedBehavior*> behaviors;
  std::vector<AnimatedBehavior*> overlayBehaviors;
  std::vector<FrameBuffer*> frameBuffers;
  // Runs once per drawn frame, after behaviors draw and before any
  // FrameBuffer flush, so a brightness decision (power governor) reaches the
  // drivers ahead of that frame's pixel writes.
  void (*preFlushHook)() = nullptr;
  bool startupComplete = false;
  bool behaviorsComputed = false;
  unsigned long lastDrawTimeMs = 0;
  bool homeMode = false;
  // True while home mode is on because the app is showing its Home Mode page.
  bool homePreview = false;

  Compositor();

  // True while this lamp is wisp-painted per the freshest MSG_WISP_STATE.
  // Drives the app indicator, the snafu dots yield, and (via wispPresence)
  // the expression wisp-dim. Fresh STATE absence eases it home; STATE
  // silence ages it out.
  bool wispActive() const;
  Color wispStateBaseColor() const { return wispStateBase_; }
  Color wispStateShadeColor() const { return wispStateShade_; }
  const LayerStack& wispLayer(bool base) const {
    return base ? wispBaseStack_ : wispShadeStack_;
  }

  /**
   * @param [in] inBehaviors list of behaviors to execute in priority sequence. last item
   *             is highest priority
   * @param [in] inFrameBuffers of all frame buffers used by the lamp - this helps to
   *             support multi strip lamps
   * @param [in] inUnderlayBehaviors caller-provided underlay behaviors (e.g. IdleBehavior),
   *             one per frame buffer. The compositor wires context and stores the pointers.
   * @param [in] inStartupBehaviors caller-provided startup behaviors (e.g. FadeInBehavior),
   *             one per frame buffer. Played instead of the normal behavior stack until
   *             startupComplete is set.
   * @param [in] homeMode if true the home-mode suppression policy applies (see setHomeModePolicy)
   */
  void begin(std::vector<AnimatedBehavior*> inBehaviors,
             std::vector<FrameBuffer*> inFrameBuffers,
             std::vector<AnimatedBehavior*> inUnderlayBehaviors,
             std::vector<AnimatedBehavior*> inStartupBehaviors,
             bool homeMode = false);

  /**
   * Build frames and draw them on the LEDs.
   */
  void tick();

  /**
   * Update home mode state dynamically.
   * @param [in] homeMode new home mode state
   */
  void setHomeMode(bool homeMode);

  /**
   * Update the home-mode suppression policy.
   * @param [in] socialDisabled whether social greetings are suppressed in home mode
   * @param [in] disabledIds expression type ids suppressed in home mode
   */
  void setHomeModePolicy(bool socialDisabled, std::vector<std::string> disabledIds);

  // Home mode is active with social off: the lamp ignores other lamps.
  bool homeSuppressesSocial() const { return homeMode && homeSocialDisabled_; }

  /**
   * True when home mode is active AND `type` is in the home-disabled set.
   * Shared with the render-skip decision so the mesh receive path can drop
   * a home-disabled cascade at receipt instead of creating a squatting
   * transient the render gate then refuses to draw.
   */
  bool homeModeSkipsType(const std::string& type) const {
    return homeMode && homeModeExpressionSkips(type.c_str(), homeDisabledExprIds_);
  }

  // homeModeTick for `b` under this compositor's home state and policy.
  HomeModeTick homeModeTickFor(AnimatedBehavior* b) const {
    return homeModeTick(homeMode, homeSocialDisabled_, b->isSocialBehavior(),
                        b->playsAppGreeting(),
                        homeModeExpressionSkips(b->homeModeExpressionId(), homeDisabledExprIds_));
  }

  // homeModeBlocksRemote over this compositor's live home state and policy.
  bool blocksRemote(TransientSource source) const {
    return homeModeBlocksRemote(homeMode, homeSocialDisabled_, source);
  }

  // True while blocksRemote(source) and outside the app's Home Mode preview:
  // only then is a blocked arrival consumed or a running peer transient
  // retired, so previewing home mode changes nothing that outlasts it.
  bool discardsRemote(TransientSource source) const {
    return blocksRemote(source) && !homePreview;
  }

  /**
   * Mark the expression band bounds [start, end) within the
   * behavior list, keeping the band contiguous as behaviors are
   * added or removed at runtime.
   */
  void setExpressionBand(size_t start, size_t end);

  /**
   * Insert a new expression behavior at the expression-band end.
   * Increments the band-end index.
   */
  void addBehavior(AnimatedBehavior* b);

  /**
   * Insert a base-scene behavior at the expression-band start so it
   * draws below every expression (a variant's custom base scene, e.g.
   * snafu's dots). Shifts the whole band up by one.
   */
  void addBaseBehavior(AnimatedBehavior* b);

  /**
   * Remove a behavior pointer. If it was within the expression band,
   * decrements the band-end index.
   */
  void removeBehavior(AnimatedBehavior* b);

  /**
   * Mutable access to the shared BehaviorContext. Used by
   * lamp.cpp at boot to wire the ExpressionManager pointer
   * and let ExpressionManager publish its frame buffer list. The
   * compositor itself self-publishes via the constructor.
   */
  BehaviorContext& behaviorContext() { return context_; }

  // Edge-triggered on MSG_WISP_STATE receipt (from drainWispState), never
  // per-frame. clearWispState marks a fresh STATE that no longer paints this
  // lamp so presence eases home.
  void clearWispState(uint32_t nowMs) {
    wispStatePresent_ = false;
    wispStateFreshMs_ = nowMs;
  }
  // Feed a drained STATE slot: this lamp's RGBW entry is applied, with its
  // drift fade record when the slot holds one; its absence clears. Returns
  // whether the slot paints `selfMac`. Header-inline as the native-test seam
  // for the drain.
  bool applyWispStateSlot(const PendingWispState& slot, const uint8_t selfMac[6],
                          uint32_t nowMs) {
    Color base;
    Color shade;
    if (!wispStateColorsFor(slot, selfMac, base, shade)) {
      clearWispState(nowMs);
      return false;
    }
    // A fade only carries over between frames of one unbroken paint from one
    // wisp; a lamp that was dropped or changed wisps takes the plain colour.
    const bool continuing =
        wispStatePresent_ && std::memcmp(wispStateSource_, slot.sourceMac, 6) == 0;
    const lamp_protocol::WispDriftFade& rec = slot.fadeRecord;
    const Color toBase(rec.base[0], rec.base[1], rec.base[2], rec.base[3]);
    const Color toShade(rec.shade[0], rec.shade[1], rec.shade[2], rec.shade[3]);
    wispFadeSync_ = feedWispSurface(
        wispBaseStack_, wispBasePresence_.value(), base,
        !continuing || !(base == wispStateBase_), continuing, slot.fade, toBase,
        rec.leftMs, nowMs);
    feedWispSurface(wispShadeStack_, wispShadePresence_.value(), shade,
                    !continuing || !(shade == wispStateShade_), continuing,
                    slot.fade, toShade, rec.leftMs, nowMs);
    const bool record = slot.fade == lamp_protocol::WispDriftFadeLookup::Record;
    if (record || !wispBaseStack_.wispFadeRunning(nowMs)) {
      wispStateHeadingBase_ = record ? toBase : base;
    }
    if (record || !wispShadeStack_.wispFadeRunning(nowMs)) {
      wispStateHeadingShade_ = record ? toShade : shade;
    }
    std::memcpy(wispStateSource_, slot.sourceMac, 6);
    wispStateBase_ = base;
    wispStateShade_ = shade;
    wispStatePresent_ = true;
    wispStateFreshMs_ = nowMs;
    return true;
  }
  // The colours the wisp is taking this lamp to: a drift fade's target while
  // one runs, else the last STATE colour. Steady across a fade's frames.
  Color wispStateHeadingBaseColor() const { return wispStateHeadingBase_; }
  Color wispStateHeadingShadeColor() const { return wispStateHeadingShade_; }
  // What the last STATE's drift fade record did to the base surface.
  WispFadeSync wispFadeSync() const { return wispFadeSync_; }

  // Advance the wisp presence and layers one frame, each surface's presence
  // heading to 1 while held. Header-inline as the native-test seam.
  void tickWisp(uint32_t nowMs, bool baseHeld, bool shadeHeld) {
    wispBasePresence_.setTarget(baseHeld ? 1.0f : 0.0f);
    wispShadePresence_.setTarget(shadeHeld ? 1.0f : 0.0f);
    wispBasePresence_.tick();
    wispShadePresence_.tick();
    wispShadeStack_.tick(wispShadePresence_.value(), nowMs);
    wispBaseStack_.tick(wispBasePresence_.value(), nowMs);
  }

  // wispActive at nowMs.
  bool wispStateFresh(uint32_t nowMs) const {
    return wispStatePresent_ && (nowMs - wispStateFreshMs_) < kWispStateFreshMs;
  }
  // Retires the last STATE's stamp so a silent wisp cannot read fresh again
  // one millis() wrap later. Called every frame.
  void retireWispState(uint32_t nowMs) {
    wispStateFreshMs_ = lampos::retireStamp(nowMs, wispStateFreshMs_);
  }

  // Failsafe window: presence ages out this long after the last STATE frame
  // when frames stop arriving. Sized to ride out coex loss without flapping; a
  // wisp that vanishes with no clearing frame holds its paint this long before
  // easing home.
  static constexpr uint32_t kWispStateFreshMs = 60000;
};
}  // namespace lamp
