#pragma once
#include <memory>

#include "components/network/ble/ble_control.hpp"
#include "config/config.hpp"
#include "core/compositor.hpp"
#include "core/lamp.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/expression_registry.hpp"
#include "lamps/lioness/lions_ambient_behavior.hpp"
#include "lamps/lioness/lions_greeting_behavior.hpp"

extern lamp::Compositor compositor;
extern lamp::MeshLink meshLink;
extern lamp::ExpressionManager expressionManager;

namespace lamp {

// Lioness: fixed-install, locked custom variant. Shade (pin14, 36px) + two
// base strips: "Main" (pin4, 32px, broadcast) and "Lions" (pin5, 18px).
class LionessLamp : public Lamp {
 public:
  LionessLamp() : Lamp(HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=36, .name="Shade"},
      {.role=Surface::Base,  .pin=4,  .byteOrder=ByteOrder::GRBW, .pixelCount=32, .name="Main", .broadcast=1},
      {.role=Surface::Base,  .pin=5,  .byteOrder=ByteOrder::GRBW, .pixelCount=18, .name="Lions"},
    },
    .maxBrightness = 230,
    .supplyBudgetMa = 1400,
  }) {}

 protected:
  Features featuresEnabled() const override {
    return Features::All
      & ~Features::SocialBehavior;   // replaced by the Lions ambient Greetable + greeting
  }

  Config::Defaults defaults() const override {
    return {
      .name = "lioness",
      .setup = true,
      .baseSegments = {
        {"Main",  32, "#30078300,#64149600"},   // deep purple -> violet
        {"Lions", 18, "#30078300"},             // ambient behavior repaints this each frame; seed only shows until first draw
      },
      .shadeSegments = {
        {"Shade", 36, "#5A170000"},
      },
    };
  }

  void registerExpressions(ExpressionRegistry& reg) override {
    Lamp::registerExpressions(reg);                 // shared editable catalog
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    if (!baseFb()) return;
    ambient_  = std::make_unique<lioness::LionsAmbientBehavior>(baseFb());
    greeting_ = std::make_unique<lioness::LionsGreetingBehavior>(baseFb());
    ambient_->setMeshLink(&meshLink);
    ambient_->setGreeting(greeting_.get());
    greeting_->setExpressionManager(&expressionManager);

    compositor.addBaseBehavior(ambient_.get());   // always-PLAYING base scene (addBaseBehavior ONLY)
    b.add(greeting_.get());                        // STOPPED until arrival (b.add ONLY)

    compositor.behaviorContext().greeting = ambient_.get();   // Greetable slot: onColorInfo -> zones
    ble_control::setGreetingStateProvider(
        [this]() { return greeting_->greetingState(); });
    greeting_->setOnGreetingChangeCallback(
        []() { ble_control::notifyStateChange(); });
  }

 private:
  std::unique_ptr<lioness::LionsAmbientBehavior>  ambient_;
  std::unique_ptr<lioness::LionsGreetingBehavior> greeting_;
};

}  // namespace lamp
