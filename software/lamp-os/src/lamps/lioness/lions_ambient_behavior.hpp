#pragma once
#include <array>
#include <vector>

#include "behaviors/greetable.hpp"
#include "core/animated_behavior.hpp"
#include "expressions/primitives.hpp"
#include "lamps/lioness/lions_director.hpp"
#include "util/color.hpp"

namespace lamp { class MeshLink; }

namespace lamp { namespace lioness {

class LionsGreetingBehavior;

// Peer-switch crossfade length, in ~60Hz compositor frames. Long enough that a
// lion easing onto a new lamp reads as a slow dissolve, not a snap. Bench-tunable.
constexpr uint32_t kCrossfadeFrames = 90;   // ~1.5s at 60Hz

// How long an unanswered MSG_COLOR_QUERY blocks a re-query of the same peer.
// Shorter re-asks a silent peer (legacy, out of ESP-NOW range) more often, each
// send taking a WiFi TX buffer; longer leaves a lion on Main longer after a
// lost reply. Bench-tunable.
constexpr uint32_t kColorQueryRetryMs = 3000;

// Always-PLAYING base scene owning the Lions base segment (the 2nd base
// segment). Each 6px lion zone mirrors an assigned nearby peer's base gradient;
// idle zones mirror Main; peer switches crossfade. Registered as the lamp's
// Greetable because the zone gradients need MSG_COLOR_INFO stops for every peer
// it queries; the arrival greeting is delegated to a separate
// LionsGreetingBehavior.
class LionsAmbientBehavior : public AnimatedBehavior, public lamp::Greetable {
 public:
  explicit LionsAmbientBehavior(FrameBuffer* baseFb);

  void control() override;
  void draw() override;

  void triggerGreeting(const lamp::PeerView& peer) override;   // -> greeting_
  lamp::GreetingState greetingState() const override;          // -> greeting_
  void onColorInfo(const uint8_t srcMac[6],
                   const std::vector<Color>& baseStops,
                   const std::vector<Color>& shadeStops) override;

  void setMeshLink(lamp::MeshLink* m) { meshLink_ = m; }
  void setGreeting(LionsGreetingBehavior* g) { greeting_ = g; }

 private:
  static constexpr uint32_t kAmbientFrames = 600;   // loop length; never playOnce

  Color sampleMainColor() const;
  const std::vector<Color>* stopsFor(const Mac& m) const;
  bool shown(const Mac& m) const;
  void requestStops(const Mac& m, uint32_t nowMs);

  int lionsSegIndex_ = -1;
  int mainSegIndex_  = -1;

  LionsDirector director_;
  std::vector<Mac> nearby_;                        // reused control() roster snapshot

  std::array<Zone, 3> zones_{};                    // fixed 3-lion tiling, computed once
  std::array<std::vector<Color>, 3> curStops_{};   // stops each lion renders now
  std::array<std::vector<Color>, 3> prevStops_{};  // pre-switch stops (crossfade src)
  std::array<uint32_t, 3> switchFrame_{};          // frames since each lion switched
  std::vector<Color> scene_;                       // last full 18px ambient scene
  std::vector<Color> from_;                        // reused crossfade-source buffer

  // Base stops for the peers the lions show; kIdle marks a free entry. An
  // entry a lion shows is never evicted.
  struct Cached { Mac mac; std::vector<Color> baseStops; };
  std::array<Cached, 3> stopCache_{};

  // Outstanding colour queries, a pool shared by the three lions. A slot is
  // busy until its peer answers or kColorQueryRetryMs passes, so a silent peer
  // is asked at most once per window and at most kQuerySlots queries are
  // outstanding at once.
  static constexpr size_t kQuerySlots = 3;
  struct PendingQuery { Mac mac; uint32_t sentMs; };
  std::array<PendingQuery, kQuerySlots> pending_{};

  lamp::MeshLink* meshLink_ = nullptr;
  LionsGreetingBehavior* greeting_ = nullptr;
};

}}  // namespace lioness, namespace lamp
