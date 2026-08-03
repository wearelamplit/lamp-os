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

  struct Cached { Mac mac; std::vector<Color> baseStops; };
  std::vector<Cached> stopCache_;                  // per-peer base stops (<=4 live)

  lamp::MeshLink* meshLink_ = nullptr;
  LionsGreetingBehavior* greeting_ = nullptr;
};

}}  // namespace lioness, namespace lamp
