#include "lamps/lioness/lions_ambient_behavior.hpp"

#include <Arduino.h>
#include <cstring>

#include "components/network/mesh/mesh_link.hpp"
#include "config/config_types.hpp"
#include "core/behavior_context.hpp"
#include "core/frame_buffer.hpp"
#include "expressions/primitives.hpp"
#include "lamps/lioness/lions_greeting_behavior.hpp"
#include "lamps/lioness/lions_scene.hpp"
#include "util/fade.hpp"

namespace lamp { namespace lioness {

namespace {
Mac macOf(const lamp::PeerView& p) {
  Mac m{};
  if (p.hasMac && p.mac) std::memcpy(m.data(), p.mac, 6);
  return m;
}
}  // namespace

LionsAmbientBehavior::LionsAmbientBehavior(FrameBuffer* baseFb)
    : AnimatedBehavior(baseFb, kAmbientFrames, /*inAutoPlay=*/true) {
  for (size_t k = 0; fb && k < fb->segments.size(); ++k) {
    const char* n = fb->segments[k].name ? fb->segments[k].name : "";
    if (std::strcmp(n, "Lions") == 0) lionsSegIndex_ = static_cast<int>(k);
    else if (std::strcmp(n, "Main") == 0) mainSegIndex_ = static_cast<int>(k);
  }
  if (lionsSegIndex_ >= 0) {
    const uint16_t px = fb->segments[lionsSegIndex_].pixelCount;
    const std::vector<Zone> z = evenZones(3, px);
    for (size_t i = 0; i < zones_.size() && i < z.size(); ++i) zones_[i] = z[i];
    scene_.assign(px, kBaseDefaultColor);
    from_.assign(px, kBaseDefaultColor);
  }
}

const std::vector<Color>* LionsAmbientBehavior::stopsFor(const Mac& m) const {
  for (const Cached& c : stopCache_) if (c.mac == m) return &c.baseStops;
  return nullptr;
}

bool LionsAmbientBehavior::shown(const Mac& m) const {
  for (const Mac& c : director_.current) if (c == m) return true;
  return false;
}

void LionsAmbientBehavior::requestStops(const Mac& m, uint32_t nowMs) {
  if (!meshLink_) return;
  PendingQuery* slot = nullptr;
  for (PendingQuery& q : pending_) {
    const bool busy = !(q.mac == kIdle) && nowMs - q.sentMs < kColorQueryRetryMs;
    if (busy && q.mac == m) return;
    if (!busy && !slot) slot = &q;
  }
  if (!slot) return;
  meshLink_->sendColorQuery(m.data());  // fill via onColorInfo
  *slot = {m, nowMs};
}

Color LionsAmbientBehavior::sampleMainColor() const {
  if (!fb || mainSegIndex_ < 0) return kBaseDefaultColor;
  const StripSegment& mg = fb->segments[mainSegIndex_];
  const uint16_t mid = mg.offset + mg.pixelCount / 2;
  return (mid < fb->buffer.size()) ? fb->buffer[mid] : kBaseDefaultColor;
}

void LionsAmbientBehavior::onColorInfo(const uint8_t srcMac[6],
                                       const std::vector<Color>& baseStops,
                                       const std::vector<Color>& /*shadeStops*/) {
  Mac m{}; std::memcpy(m.data(), srcMac, 6);
  for (PendingQuery& q : pending_) if (q.mac == m) q = {};
  Cached* slot = nullptr;
  for (Cached& c : stopCache_) if (c.mac == m) slot = &c;
  for (Cached& c : stopCache_) if (!slot && c.mac == kIdle) slot = &c;
  for (Cached& c : stopCache_) if (!slot && !shown(c.mac)) slot = &c;
  if (!slot) return;
  slot->mac = m;
  slot->baseStops = baseStops;
}

void LionsAmbientBehavior::triggerGreeting(const lamp::PeerView& peer) {
  if (greeting_) greeting_->startGreeting(peer);
}

lamp::GreetingState LionsAmbientBehavior::greetingState() const {
  return greeting_ ? greeting_->greetingState() : lamp::GreetingState{};
}

void LionsAmbientBehavior::control() {
  if (!fb || lionsSegIndex_ < 0 || !context_) return;

  nearby_.clear();
  context_->forEachNearby([&](const lamp::PeerView& p) {
    Mac m = macOf(p);
    if (!(m == kIdle)) nearby_.push_back(m);
    return false;
  });

  const uint32_t now = millis();
  const uint8_t changed = director_.tick(now, nearby_);
  for (int i = 0; i < 3; ++i) {
    if (changed & (1u << i)) {
      prevStops_[i].swap(curStops_[i]);
      switchFrame_[i] = 0;
    }
    const Mac& m = director_.current[i];
    const std::vector<Color>* cached = (m == kIdle) ? nullptr : stopsFor(m);
    if (cached) {
      curStops_[i].assign(cached->begin(), cached->end());
    } else {
      curStops_[i].clear();
      if (!(m == kIdle)) requestStops(m, now);
    }
  }
}

void LionsAmbientBehavior::draw() {
  if (!fb || lionsSegIndex_ < 0) { nextFrame(); return; }
  const StripSegment& lg = fb->segments[lionsSegIndex_];
  const Color mainColor = sampleMainColor();

  for (size_t z = 0; z < zones_.size(); ++z)
    renderZone(scene_, zones_[z], curStops_[z], mainColor);

  for (size_t z = 0; z < zones_.size(); ++z) {
    if (switchFrame_[z] >= kCrossfadeFrames) continue;
    const Zone& zn = zones_[z];
    renderZone(from_, zn, prevStops_[z], mainColor);
    for (uint16_t i = 0; i < zn.size(); ++i) {
      scene_[zn.posMin + i] = fade(from_[zn.posMin + i], scene_[zn.posMin + i],
                                   kCrossfadeFrames, switchFrame_[z]);
    }
    ++switchFrame_[z];
  }

  for (uint16_t i = 0; i < lg.pixelCount && lg.offset + i < fb->buffer.size(); ++i) {
    fb->buffer[lg.offset + i] = scene_[i];
  }
  nextFrame();
}

}}  // namespace lioness, namespace lamp
