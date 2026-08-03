#include "lamps/lioness/lions_greeting_behavior.hpp"

#include <Arduino.h>
#include <cstring>

#include "core/behavior_context.hpp"
#include "core/frame_buffer.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "lamps/lioness/lions_greeting.hpp"    // pulseEnvelope + timing constants
#include "util/fade.hpp"

namespace lamp { namespace lioness {

LionsGreetingBehavior::LionsGreetingBehavior(FrameBuffer* baseFb)
    : AnimatedBehavior(baseFb, kGreetFrames, /*inAutoPlay=*/false) {
  for (size_t k = 0; fb && k < fb->segments.size(); ++k) {
    const char* n = fb->segments[k].name ? fb->segments[k].name : "";
    if (std::strcmp(n, "Lions") == 0) { lionsSegIndex_ = static_cast<int>(k); break; }
  }
}

lamp::GreetingState LionsGreetingBehavior::greetingState() const {
  const bool playing = (animationState == PLAYING ||
                        animationState == PLAYING_ONCE ||
                        animationState == STOPPING);
  if (!playing) return {};
  lamp::GreetingState gs;
  gs.active     = true;
  gs.peerLampId = greetLampId_;
  gs.kind       = "pulse";
  return gs;
}

void LionsGreetingBehavior::startGreeting(const lamp::PeerView& peer) {
  greetColor_  = peer.baseColor;
  greetLampId_ = peer.lampId;
  frame = 0;
  playOnce();
  if (expr_ && peer.hasMac) {                    // shade glitches with newcomer colour
    lamp::ExpressionInvocation inv;
    inv.type   = "glitchy";
    inv.colors = {greetColor_};
    inv.target = static_cast<uint8_t>(TARGET_SHADE);
    expr_->triggerInvocation(inv, peer.mac, /*broadcast=*/false);
  }
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
}

void LionsGreetingBehavior::control() {
  if (!fb || lionsSegIndex_ < 0 || !context_) return;

  // Latest-wins: forEachArrival hands one ungreeted near arrival per tick; a
  // newer one mid-pulse restarts the pulse (startGreeting resets frame to 0).
  context_->forEachArrival(5000, [this](const lamp::PeerView& p) {
    startGreeting(p);
    return true;   // ack this peer
  });

  const bool active = (animationState == PLAYING ||
                       animationState == PLAYING_ONCE ||
                       animationState == STOPPING);
  if (!active && greetingWasActive_) {
    greetLampId_.clear();
    if (onGreetingChange_) onGreetingChange_();
  }
  greetingWasActive_ = active;
}

void LionsGreetingBehavior::draw() {
  if (!fb || lionsSegIndex_ < 0) { nextFrame(); return; }
  const StripSegment& lg = fb->segments[lionsSegIndex_];
  const uint8_t bri = pulseEnvelope(frame);
  const uint32_t pulseWindow = kPulses * kFramesPerPulse;

  for (uint16_t i = 0; i < lg.pixelCount && lg.offset + i < fb->buffer.size(); ++i) {
    const Color ambient = fb->buffer[lg.offset + i];   // ambient base scene drew first
    Color pulse(static_cast<uint8_t>(greetColor_.r * bri / 255),
                static_cast<uint8_t>(greetColor_.g * bri / 255),
                static_cast<uint8_t>(greetColor_.b * bri / 255),
                static_cast<uint8_t>(greetColor_.w * bri / 255));
    fb->buffer[lg.offset + i] = (frame >= pulseWindow)
        ? fade(pulse, ambient, kGreetEaseFrames, frame - pulseWindow)
        : pulse;
  }
  nextFrame();   // playOnce returns to STOPPED after kGreetFrames; draw then skipped
}

}}  // namespace lioness, namespace lamp
