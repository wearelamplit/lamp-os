// WispOpDispatcher — parse + route a MSG_CONTROL_OP payload (JSON) on the
// wisp side (BLE app pane → lamp → mesh → wisp).
//
// On a setWifi op the dispatcher persists once via WispConfig then kicks both
// downstream consumers (WifiLink for the STA association, StageBeacon for the
// BLE creds advert). Those two are pointers so tests exercising only
// zone/source/palette flows can pass nullptr; only the setWifi path derefs
// them, and a null there logs and skips the kick without aborting persistence.
//
// Wire format: plaintext JSON like
//   {"char":"wispOp","op":"setZone","zoneId":3}
//   {"char":"wispOp","op":"setWifi","ssid":"foo","pw":"bar"}
// Any other "char" (including gossip-relayed wispStatus echoes) returns
// Ignored silently.

#pragma once

#include <cstddef>
#include <cstdint>

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
  Malformed,           // JSON parse failed or required field missing
};

class WispOpDispatcher {
 public:
  explicit WispOpDispatcher(WispConfig& config) : config_(config) {}

  // Optional wiring for the setWifi op chain. Pass nullptr in tests that
  // don't exercise the setWifi path. The dispatcher does not own either
  // pointer; the caller (main.cpp) owns the globals.
  void setWifiSinks(WifiLink* wifi, StageBeacon* stage) {
    wifiLink_ = wifi;
    stageBeacon_ = stage;
  }

  // payload + len point into the recv buffer. The function copies what it
  // needs and returns; the caller is free to release the buffer after.
  DispatchResult dispatch(const uint8_t* payload, size_t len);

 private:
  WispConfig& config_;
  WifiLink*    wifiLink_   = nullptr;
  StageBeacon* stageBeacon_ = nullptr;
};

}  // namespace wisp
