#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "behaviors/greetable.hpp"
#include "core/animated_behavior.hpp"
#include "components/network/mesh/nearby_lamps.hpp"
#include "util/color.hpp"

namespace lamp { namespace snafu {

// Snafu greeting: on arrival plays a local scramble -> fade to peer color ->
// hold -> ease. Stages 2/3 fire opportunistically after 700/1400 ms via
// ExpressionManager (Ambivert/Extrovert tiers only).
class Greeting : public AnimatedBehavior, public lamp::Greetable {
 public:
  Greeting(FrameBuffer* inFb)
    : AnimatedBehavior(inFb, /*inFrames=*/300, /*inAutoPlay=*/false) {}

  void draw() override;
  void control() override;

  // Greetable: play a greeting for the given peer immediately.
  void triggerGreeting(const lamp::NearbyLamp& peer) override;

  lamp::GreetingState greetingState() const override;

  void setOnGreetingChangeCallback(std::function<void()> fn) { onGreetingChange_ = std::move(fn); }

 private:
  void doGreet(const lamp::NearbyLamp& peer);
  void tickStages();

  static constexpr uint32_t kGlitchFrames    = 23;
  static constexpr uint32_t kEaseFrames      = 60;
  static constexpr uint32_t kFadeInFrames    = kGlitchFrames + kEaseFrames;
  static constexpr uint32_t kFastGlitchFrames = 12;
  static constexpr uint32_t kBleMaxAgeMs     = 5000;
  static constexpr uint32_t kEspNowMaxAgeMs  = 5000;
  static constexpr uint32_t kStage2Ms        = 700;
  static constexpr uint32_t kStage3Ms        = 1400;

  // Glitch gradient (0,45,200,0)..(180,0,60,0), rebuilt per greeting.
  std::vector<Color> glitchColors_;
  uint32_t glitchOffset_ = 0;

  Color arrivedColor_;

  // Per-greeting staged state. Cleared in doGreet.
  uint8_t     greetedMac_[6] = {0};
  bool        greetedHasMac_ = false;
  std::string greetedBdAddr_;
  uint32_t    greetStartMs_  = 0;
  bool        stage2Done_    = false;
  bool        stage3Done_    = false;

  bool                  greetingWasActive_ = false;
  std::function<void()> onGreetingChange_;
};

} }  // namespace snafu, namespace lamp
