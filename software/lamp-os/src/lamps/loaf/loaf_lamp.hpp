#pragma once
#include <memory>

#include "config/config.hpp"
#include "core/compositor.hpp"
#include "core/lamp.hpp"
#include "lamps/loaf/loaf_ring_behavior.hpp"

extern lamp::Compositor compositor;

namespace lamp {

// Loaf: a deliberately minimal user-configurable variant. Two rings drawn as
// seamless circular gradients of the user's own colors; the 40px base ring
// slowly rotates, the 12px shade is static.
class LoafLamp : public Lamp {
 public:
  LoafLamp() : Lamp(HwConfig{
    .strips = {
      {.role=Surface::Base,  .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=40, .name="Base", .broadcast=1},
      {.role=Surface::Shade, .pin=12, .byteOrder=ByteOrder::GRBW, .pixelCount=12, .name="Shade"},
    },
    .maxBrightness = 230,
    .supplyBudgetMa = 1400,
  }) {}

 protected:
  Features featuresEnabled() const override {
    return Features::All & ~Features::DefaultExpressions;   // no expressions over the ring
  }

  Config::Defaults defaults() const override {
    return {
      .name = "stray",             // fresh-lamp sentinel (same as standard)
      .shadeColor = "#5A170000",
      .basePx  = 40,
      .shadePx = 12,
    };
  }

  void createBehaviors(BehaviorStackBuilder&) override {
    if (baseFb()) {
      base_ = std::make_unique<loaf::LoafRingBehavior>(
          baseFb(), /*isBase=*/true, kBaseRevolutionMs, kCompanionRevolutionMs);
      compositor.addBaseBehavior(base_.get());
    }
    if (shadeFb()) {
      shade_ = std::make_unique<loaf::LoafRingBehavior>(shadeFb(), /*isBase=*/false, /*revMs=*/0);
      compositor.addBaseBehavior(shade_.get());
    }
  }

 private:
  // Base-ring revolution period. Bench-tune: lower = faster spin.
  static constexpr uint32_t kBaseRevolutionMs = 25000;
  // Faster period the base ring eases to while another loaf is nearby.
  static constexpr uint32_t kCompanionRevolutionMs = 8000;

  std::unique_ptr<loaf::LoafRingBehavior> base_;
  std::unique_ptr<loaf::LoafRingBehavior> shade_;
};

}  // namespace lamp
