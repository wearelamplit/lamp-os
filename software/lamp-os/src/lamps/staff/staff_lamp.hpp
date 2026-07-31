#pragma once
#include <memory>

#include "config/config.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"
#include "core/lamp.hpp"
#include "expressions/bloom/bloom_expression.hpp"
#include "expressions/expression.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/expression_registry.hpp"
#include "lamps/staff/staff_input_behavior.hpp"

extern lamp::Compositor compositor;
extern lamp::Config config;
extern lamp::ExpressionManager expressionManager;

namespace lamp {

// Tactile staff lamp: a shade + base strip plus physical inputs (stoke
// button, two touch pads). Keeps the standard social stack; adds local
// gesture control (stoke brightness, mood scrub, touch-unlock, pad dims)
// wired in createBehaviors.
//
// ponytail: GPIO12 (shade LED) and GPIO15 (bottom touch pad) sit on ESP32
// boot straps. As-wired and field-proven for GPIO12 (standard/snafu drive
// it); GPIO15 pulled low only suppresses the boot log. First suspect on
// cold-boot flake.
class StaffLamp : public Lamp {
 public:
  enum InputId : uint8_t { kStoke = 1, kTopPad = 3, kBottomPad = 4 };

  StaffLamp() : Lamp(HwConfig{
    .strips = {
      {.role=Surface::Shade, .pin=12, .byteOrder=ByteOrder::GRBW, .name="Shade", .broadcast=1},
      {.role=Surface::Base,  .pin=14, .byteOrder=ByteOrder::GRBW, .name="Base", .reversed=true},
    },
    .inputs = {
      {.id=kStoke,     .type=InputType::Button, .pin=19},
      {.id=kTopPad,    .type=InputType::Touch,  .pin=4},
      {.id=kBottomPad, .type=InputType::Touch,  .pin=15},
    },
    .maxBrightness = 230,
    .supplyBudgetMa = 1400,
  }) {}

 protected:
  Features featuresEnabled() const override { return Features::All; }

  void registerExpressions(ExpressionRegistry& reg) override {
    Lamp::registerExpressions(reg);  // the shared editable catalog
    reg.add(BloomExpression::classDescriptor());  // internal; skipped by the catalog
  }

  Config::Defaults defaults() const override {
    return {
      .name = "staff",
      .baseColor = "#30078300",
      .shadeColor = "#5A170000",
      .basePx = 35,
      .shadePx = 38,
      .setup = true,   // fixed install: curated colors, no random roll
    };
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    auto* stoke = inputById(kStoke);
    auto* topPad = inputById(kTopPad);
    auto* bottomPad = inputById(kBottomPad);
    input_ = std::make_unique<staff::StaffInputBehavior>(
        shadeFb(), config,
        stoke ? stoke->asButton() : nullptr,
        topPad ? topPad->asTouch() : nullptr,
        bottomPad ? bottomPad->asTouch() : nullptr);
    b.add(input_.get());

    // Reference reactive-social example: a fond-or-better peer arriving nearby
    // gets a wordless white bloom over whatever else is rendering. onArrival
    // dedups + re-arms in the framework; the callback just filters by
    // disposition and fires the internal bloom as a one-shot transient.
    compositor.behaviorContext().onArrival([](const lamp::PeerView& peer) {
      constexpr uint8_t kDispositionFond = 4;
      if (config.getDisposition(peer.lampId) < kDispositionFond) return;
      lamp::ExpressionInvocation inv;
      inv.type = "bloom";
      inv.target = lamp::TARGET_BOTH;
      expressionManager.triggerInvocation(inv, peer.mac, /*broadcast=*/false);
    });
  }

 private:
  std::unique_ptr<staff::StaffInputBehavior> input_;
};

}  // namespace lamp
