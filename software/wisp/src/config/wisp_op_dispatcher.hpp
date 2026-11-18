// WispOpDispatcher parses + routes a MSG_CONTROL_OP payload (JSON).
// Wire: {"char":"wispOp","op":"setSource","mode":"osc"} etc.
// WifiLink/StageBeacon pointers are nullable for tests that skip the setWifi path.

#pragma once

#include <cstddef>
#include <cstdint>

#include "config/crypto.hpp"
#include "osc/osc_target.hpp"

namespace wisp {

class WispConfig;
class WifiLink;
class StageBeacon;

enum class DispatchResult {
  Ignored,             // payload not addressed to this wisp (e.g. wispStatus echo)
  AppliedWifiChange,   // setWifi persisted + WifiLink/StageBeacon kicked
  AppliedSourceChange, // setSource applied (Off/Manual/Osc)
  AppliedOscRole,      // setOscRole applied
  AppliedOscSettings,  // setOsc stored (sub-mode, Maestro slot, Custom address)
  AppliedOscRetarget,  // setOsc stored and moved the live Osc address
  AppliedManualPalette,// setManualPalette stored
  AppliedOffColor,     // setOffColor stored (Off-mode wisp-ring color)
  AppliedShuffle,      // shuffle: bumped shuffleSeed, re-roll queued
  AppliedDriftChange,  // setDrift applied (interval + fadePct)
  AppliedNameChange,     // setName applied
  AppliedPasswordChange, // setPassword applied
  AppliedLedStrip,       // setLedStrip applied (format + pixel count)
  AppliedBrightnessChange, // setBrightness applied (space-dim factor)
  AppliedSwatches,       // setSwatches stored (palette style)
  PollStatus,            // pollStatus: re-emit wispStatus, no state change
  PollConfig,            // pollConfig: re-emit wispConfig, no state change
  Malformed,             // JSON parse failed or required field missing
  Rejected,              // auth gate blocked (password set, plaintext not allowed)
};

// Stores the OSC sub-mode, Maestro slot (valid per maestroSlotValid) and
// Custom address ("" or valid per oscCustomAddressValid), and names the
// result the controller applies. Custom with no address stores nothing and
// is Malformed: it would listen to nothing.
DispatchResult applyOscSettings(WispConfig& config, OscMode mode, uint8_t slot,
                                const char* address);

class WispOpDispatcher {
 public:
  explicit WispOpDispatcher(WispConfig& config) : config_(config) {}

  // Optional wiring for the setWifi op chain. Pass nullptr in tests that
  // don't exercise the setWifi path. The dispatcher does not own either pointer.
  void setWifiSinks(WifiLink* wifi, StageBeacon* stage) {
    wifiLink_ = wifi;
    stageBeacon_ = stage;
  }

  // Inject a nonce ring for replay detection. Pass nullptr to skip replay detection.
  void setNonces(crypto::RecentNonces* nonces) { nonces_ = nonces; }

  // Caller may release payload after this returns.
  DispatchResult dispatch(const uint8_t* payload, size_t len);

 private:
  DispatchResult dispatchJson(const char* json, size_t len);

  WispConfig& config_;
  WifiLink*    wifiLink_   = nullptr;
  StageBeacon* stageBeacon_ = nullptr;
  crypto::RecentNonces* nonces_ = nullptr;
};

}  // namespace wisp
