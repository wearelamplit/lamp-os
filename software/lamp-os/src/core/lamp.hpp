// software/lamp-os/src/core/lamp.hpp
#pragma once

#include "core/hw_config.hpp"
#include "core/lamp_features.hpp"
#include "core/behavior_stack_builder.hpp"
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

 protected:
  virtual void createBehaviors(BehaviorStackBuilder&) = 0;
  virtual Features featuresEnabled() const { return Features::All; }
  virtual Config::Defaults defaults() const { return {}; }

 private:
  HwConfig hw_;
  // Internal framework members (compositor, BLE, mesh, OTA timing) live as
  // file-scope statics in lamp.cpp — single-Lamp-per-binary by design. The
  // extern declarations in apply_brightness.hpp etc. resolve to those
  // file-scope statics.

  // Per-slot drains (called from tick()). Each drains one pending slot
  // (BLE input, mesh input, transient-override command, etc.) and applies
  // its payload to lamp state. Bodies are line-for-line moves of the
  // original inline tick() blocks — see the invariant comment above
  // tick()'s definition for the ordering contract.
  void drainBrightness();
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
  void drainOverrideColors();
  void drainRestoreColors();
  void drainOverrideBrightness();
  void drainRestoreBrightness();
  void drainWispHello();
  void drainWispPalette();
  void drainWispClaim();
  void drainWispPaint();
  void drainWispOp();
  void drainWispStatus();
  void drainCommand();
  void drainEvent();
  void drainFirmwareControl();
};

}  // namespace lamp
