// WispOpDispatcher — parse + route a MSG_CONTROL_OP payload (JSON).
// Wire: {"char":"wispOp","op":"setZone","zoneId":3} etc.
// WifiLink/StageBeacon pointers are nullable for tests that skip the setWifi path.

#pragma once

#include <cstddef>
#include <cstdint>

#include "config/crypto.hpp"

namespace wisp {

class WispConfig;
class WifiLink;
class StageBeacon;

enum class DispatchResult {
  Ignored,             // payload not for us (e.g. wispStatus echo)
  AppliedZoneChange,   // setZone / clearZone applied
  AppliedWifiChange,   // setWifi persisted + WifiLink/StageBeacon kicked
  AppliedSourceChange, // setSource applied (Off/Manual/Aurora)
  AppliedManualPalette,// setManualPalette stored
  AppliedOffColor,     // setOffColor stored (Off-mode wisp-ring color)
  AppliedShuffle,      // shuffle: bumped shuffleSeed, re-roll queued
  AppliedDriftChange,  // setDrift applied (interval + fadePct)
  AppliedNameChange,     // setName applied
  AppliedPasswordChange, // setPassword applied
  Malformed,             // JSON parse failed or required field missing
  Rejected,              // auth gate blocked (password set, plaintext not allowed)
};

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
