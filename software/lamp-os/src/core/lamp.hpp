// software/lamp-os/src/core/lamp.hpp
#pragma once

#include <memory>
#include <vector>

#include "core/hw_config.hpp"
#include "core/lamp_features.hpp"
#include "core/behavior_stack_builder.hpp"
#include "components/input/input_source.hpp"
#include "config/config.hpp"

namespace lamp {

class FrameBuffer;  // forward
class ExpressionRegistry;  // forward

class Lamp {
 public:
  explicit Lamp(HwConfig hw) : hw_(std::move(hw)) {}
  virtual ~Lamp() = default;

  // Called from main.cpp's setup(). Wires the framework (BLE GATT,
  // ESP-NOW mesh, OTA self-health, compositor, pending + override
  // aggregates, built-in behaviors selected via featuresEnabled()).
  // Calls subclass's defaults() to seed Config before NVS load, then
  // createBehaviors() to assemble the final behavior list.
  void setup();

  // Called from main.cpp's loop(). Drains pending slots, runs OTA
  // deadline checks, ticks the compositor.
  void tick();

  // Accessors for subclasses (e.g. binding a behavior to a specific
  // FrameBuffer in createBehaviors).
  FrameBuffer* shadeFb();
  FrameBuffer* baseFb();
  const HwConfig& hw() const { return hw_; }

  // Populate `reg` with this lamp's expression catalog. The base registers
  // the five built-ins when Features::DefaultExpressions is set; a subclass
  // overrides to register its own set.
  virtual void registerExpressions(ExpressionRegistry& reg);

  // Look up a boot-built input driver by its HwConfig id, or null. A variant
  // binds gestures in createBehaviors via source->asButton()/asTouch().
  input::InputSource* inputById(uint8_t id) const;

 protected:
  virtual void createBehaviors(BehaviorStackBuilder&) = 0;
  virtual Features featuresEnabled() const { return Features::All; }
  virtual Config::Defaults defaults() const { return {}; }

 private:
  HwConfig hw_;
  // Input drivers built once from hw_.inputs at setup(); ticked before the
  // compositor each loop. Empty on variants that declare no inputs.
  std::vector<std::unique_ptr<input::InputSource>> inputs_;
  void tickInputs(uint32_t nowMs);
  // Internal framework members (compositor, BLE, mesh, OTA timing) live as
  // file-scope statics in lamp.cpp, single-Lamp-per-binary by design. The
  // extern declarations in apply_brightness.hpp etc. resolve to those
  // file-scope statics.

  // Recompute s_hwMaxBrightness from the variant cap and config.brightnessCeiling.
  // Call at setup and after any settings-blob write so a new ceiling applies live.
  void recomputeEffectiveCeiling();

  // Per-slot drains (called from tick()). Each drains one pending slot
  // (BLE input, mesh input, transient-override command, etc.) and applies
  // its payload to lamp state. See the invariant comment above
  // tick()'s definition for the ordering contract.
  void drainBrightness();
  void drainEditSession();
  void drainShadeColors();
  void drainBaseColors();
  void drainKnockout();
  void drainExpressionOp();
  void drainCommit();
  void drainSettingsBlob();
  void drainSocialDispositions();
  void drainTestAction();
  void drainWifiOp();
  void drainInboundOp();
  void drainRemoteOp();
  void drainOverrideBrightness();
  void drainRestoreBrightness();
  void drainWispHello();
  void drainWispPalette();
  void drainWispClaim();
  void drainWispState();
  void drainWispOp();
  void drainWispStatus();
  void drainCommand();
  void drainColorQuery();
  void drainColorInfo();
  void drainEvent();
  void drainFirmwareControl();
};

}  // namespace lamp
