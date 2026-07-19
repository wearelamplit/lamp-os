#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "behaviors/greetable.hpp"
#include "core/animated_behavior.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "util/color.hpp"

namespace lamp { class MeshLink; }
namespace lamp { namespace snafu { class DotsBehavior; } }

namespace lamp { namespace snafu {

// Snafu greeting: on arrival plays a local scramble -> fade to peer color ->
// hold -> ease. Stages 2/3 fire after 700/1400 ms via ExpressionManager
// (Ambivert/Extrovert tiers only).
class Greeting : public AnimatedBehavior, public lamp::Greetable {
 public:
  Greeting(FrameBuffer* inFb)
    : AnimatedBehavior(inFb, /*inFrames=*/90, /*inAutoPlay=*/false) {}

  void draw() override;
  void control() override;

  // Greetable: play a greeting for the given peer immediately.
  void triggerGreeting(const lamp::RosterEntry& peer) override;

  lamp::GreetingState greetingState() const override;

  void setOnGreetingChangeCallback(std::function<void()> fn) { onGreetingChange_ = std::move(fn); }

  void setMeshLink(lamp::MeshLink* m) { meshLink_ = m; }
  void setDotsBehavior(DotsBehavior* d) { dots_ = d; }
  void onColorInfo(const uint8_t srcMac[6],
                   const std::vector<Color>& baseStops,
                   const std::vector<Color>& shadeStops) override;

 private:
  void doGreet(const lamp::RosterEntry& peer);
  void tickStages();

  static constexpr uint32_t kGlitchFrames    = 10;
  static constexpr uint32_t kEaseFrames      = 24;
  static constexpr uint32_t kFadeInFrames    = kGlitchFrames + kEaseFrames;
  static constexpr uint32_t kGlitchMinMs     = 120;
  static constexpr uint32_t kGlitchMaxMs     = 180;
  static constexpr uint32_t kBleMaxAgeMs     = 5000;
  static constexpr uint32_t kStage2Ms        = 700;
  static constexpr uint32_t kStage3Ms        = 1400;
  static constexpr uint32_t kCascadeStaggerMs = 90;
  static constexpr uint32_t kBorrowHoldMultiplier = 2;

  // Glitch gradient (0,45,200,0)..(180,0,60,0), rebuilt per greeting.
  std::vector<Color> glitchColors_;
  uint32_t glitchOffset_ = 0;

  Color arrivedColor_;

  // Per-greeting staged state. Cleared in doGreet.
  uint8_t     greetedMac_[6] = {0};
  bool        greetedHasMac_ = false;
  std::string greetedLampId_;
  uint32_t    greetStartMs_  = 0;
  bool        stage2Done_    = false;
  bool        stage3Done_    = false;

  bool                  greetingWasActive_ = false;
  std::function<void()> onGreetingChange_;

  lamp::MeshLink*    meshLink_ = nullptr;
  DotsBehavior*      dots_ = nullptr;
  std::vector<Color> greetedBaseStops_;
  std::vector<Color> greetedShadeStops_;
  std::vector<Color> targetColors_;
  bool targetsFromStops_ = false;
};

} }  // namespace snafu, namespace lamp
