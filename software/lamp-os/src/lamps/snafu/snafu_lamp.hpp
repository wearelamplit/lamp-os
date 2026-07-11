#pragma once
#include <memory>

#include "components/network/ble/ble_control.hpp"
#include "config/config.hpp"
#include "core/compositor.hpp"
#include "core/lamp.hpp"
#include "expressions/expression_registry.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"
#include "lamps/snafu/dots_behavior.hpp"
#include "lamps/snafu/greeting.hpp"

extern lamp::Compositor compositor;
extern lamp::Config config;

namespace lamp {

// Amanita mushroom lamp, fully custom visual stack. Shade role fans three
// physical dot strips (Small/Medium/Big Dots), each its own palette, rendered
// by the segment-aware DotsBehavior at the base-scene layer. Base role is the
// Stem (the broadcast segment) on the flat framework configurator. Greeting
// replaces SocialBehavior; the transient built-ins flash over the dots.
class SnafuLamp : public Lamp {
 public:
  SnafuLamp() : Lamp(HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=16, .name="Small Dots"},
      {.role=Surface::Shade, .pin=27, .byteOrder=ByteOrder::GRBW, .pixelCount=12, .name="Medium Dots"},
      {.role=Surface::Shade, .pin=26, .byteOrder=ByteOrder::GRBW, .pixelCount=9,  .name="Big Dots"},
      {.role=Surface::Base,  .pin=12, .byteOrder=ByteOrder::GRBW, .pixelCount=24, .name="Stem", .broadcast=1},
    },
    .maxBrightness = 180,
  }) {}

 protected:
  Features featuresEnabled() const override {
    return Features::All
      & ~Features::SocialBehavior      // replaced by snafu::Greeting
      & ~Features::DefaultExpressions; // snafu owns its own visuals
  }

  Config::Defaults defaults() const override {
    return {
      .name = "snafu",
      .setup = true,   // fixed install: always adopted, never random-rolls
      .baseSegments = {
        {"Stem", 24, "#30078300,#64149600"},                     // deep purple → violet
      },
      .shadeSegments = {
        {"Small Dots",  16, "#962d0000,#097d0400,#003c7800,#78003200"}, // orange → green → blue → magenta
        {"Medium Dots", 12, "#78100000,#a03c0000,#966e0000"},           // deep red → orange → gold
        {"Big Dots",     9, "#9b000000,#612d2c00"},                     // red → dusty rose
      },
    };
  }

  void registerExpressions(ExpressionRegistry& reg) override {
    reg.add(GlitchyExpression::descriptor());
    reg.add(PulseExpression::descriptor());
    reg.add(SpottyExpression::descriptor());
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    if (!shadeFb()) return;
    dots_ = std::make_unique<snafu::DotsBehavior>(shadeFb(), config.shade);
    compositor.addBaseBehavior(dots_.get());
    greeting_ = std::make_unique<snafu::Greeting>(shadeFb());
    b.add(greeting_.get());
    compositor.behaviorContext().greeting = greeting_.get();
    ble_control::setGreetingStateProvider(
        [this]() { return greeting_->greetingState(); });
    greeting_->setOnGreetingChangeCallback(
        []() { ble_control::notifyStateChange(); });
  }

 private:
  std::unique_ptr<snafu::DotsBehavior> dots_;
  std::unique_ptr<snafu::Greeting>     greeting_;
};

}  // namespace lamp
