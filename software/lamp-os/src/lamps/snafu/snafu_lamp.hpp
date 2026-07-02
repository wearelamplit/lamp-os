// software/lamp-os/src/lamps/snafu/snafu_lamp.hpp
#pragma once
#include <memory>
#include "core/lamp.hpp"
#include "lamps/snafu/background_fade.hpp"
#include "lamps/snafu/paint_spots.hpp"
#include "lamps/snafu/greeting.hpp"

namespace lamp {

// Amanita mushroom lamp, fully custom visual stack. Replaces built-in
// SocialBehavior + DefaultExpressions with three custom AnimatedBehavior
// subclasses.
//
// Surface layout (matches physical Amanita hardware):
//   Shade (cap): pin 12, 40 pixels, GRBW
//   Base (stem): pin 14, 40 pixels, GRBW
class SnafuLamp : public Lamp {
 public:
  SnafuLamp() : Lamp(HwConfig{
    .surfaces = {
      {.id=Surface::Shade, .pin=12, .byteOrder=ByteOrder::GRBW},
      {.id=Surface::Base,  .pin=14, .byteOrder=ByteOrder::GRBW},
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
      .name              = "snafu",
      .baseColor         = "#30078300", // purple stem — user-editable
      .shadeColor        = "#78100000", // amanita red — picker hidden in app
      .shadeColorsEditable = false,
      .basePx  = 40,                    // matches HwConfig stem
      .shadePx = 40,                    // matches HwConfig cap
    };
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    // The framework wires the BehaviorContext (including nearbyLamps) before
    // any control() runs, so snafu::Greeting's context_ is valid at runtime.
    if (shadeFb()) {
      bgFade_     = std::make_unique<snafu::BackgroundFade>(shadeFb());
      paintSpots_ = std::make_unique<snafu::PaintSpots>(shadeFb(), 24, 32);
      greeting_   = std::make_unique<snafu::Greeting>(shadeFb());
      b.add(bgFade_.get());
      b.add(paintSpots_.get());
      b.add(greeting_.get());
    }
  }

 private:
  std::unique_ptr<snafu::BackgroundFade> bgFade_;
  std::unique_ptr<snafu::PaintSpots>     paintSpots_;
  std::unique_ptr<snafu::Greeting>       greeting_;
};

}  // namespace lamp
