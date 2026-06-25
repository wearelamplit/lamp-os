// WispOpDispatcher — parse + route a MSG_CONTROL_OP payload (JSON) on the
// wisp side. The wisp acts as a CONTROL_OP receiver
// (BLE-app-pane → lamp → mesh → wisp).
//
// The dispatcher owns references to the WispConfig store plus the two
// downstream consumers of WiFi creds — WifiLink (which manages the STA
// association) and StageBeacon (which broadcasts the same creds over BLE
// for pre-mesh lamps). On a setWifi op the dispatcher persists once via
// WispConfig and then kicks both consumers to re-read.
//
// WifiLink + StageBeacon are pointers (not references) so unit-tests that
// only exercise zone/source/palette flows can pass nullptr. The setWifi
// path is the only one that derefs them; a null on that path logs and
// skips the kick without aborting the persistence step.
//
// Wire format: payload bytes are plaintext JSON like
//   {"char":"wispOp","op":"setZone","zoneId":3}
//   {"char":"wispOp","op":"clearZone"}
//   {"char":"wispOp","op":"setWifi","ssid":"foo","pw":"bar"}
// Any other "char" value (including this wisp's own gossip-relayed wispStatus
// echoes) returns Ignored silently — no warning log, since those are
// expected.

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
