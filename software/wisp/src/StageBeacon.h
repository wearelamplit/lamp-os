// StageBeacon — non-connectable BLE advertisement carrying WiFi creds for
// pre-mesh lamps to discover and auto-join.
//
// Lifted byte-for-byte from software/artnet-repeater/src/main.cpp:42-56,
// which is what the lamp-side scanner at
// software/lamp-os/src/components/network/bluetooth.cpp:39-72 expects.
//
// Magic ID 42007. Payload: <2 bytes magic LE><ssid\0><password\0>.
// Max combined ssid+password length (incl. NULs) is 26 bytes — caller
// must enforce or we silently truncate to 28 total advert bytes.
//
// Credentials source: this class binds to the shared WispConfig at begin()
// time and re-reads from it on every refresh. There is no parallel
// in-StageBeacon copy of the creds — WispConfig owns them.

#pragma once

#include <string>

namespace wisp {

class WispConfig;

class StageBeacon {
 public:
  // Initialize NimBLE and bind to the shared WispConfig. Idempotent — the
  // config binding is captured on the first call and re-binds are ignored
  // so a caller can't accidentally swap stores mid-flight.
  void begin(const std::string& deviceName, WispConfig* config);

  // Re-read creds from WispConfig and (re)start the advert. Called by
  // WispOpDispatcher after a setWifi op persists new creds, so a connected
  // operator can swap the deployment SSID and watch pre-mesh lamps follow.
  // If creds are empty, advertising stops. Safe to call any time after
  // begin(); a no-op (with log) before begin().
  void refreshAdvert();

  // Stop advertising. Safe to call before begin().
  void stop();

  bool isAdvertising() const { return advertising_; }

 private:
  bool initialized_ = false;
  bool advertising_ = false;
  std::string deviceName_;
  WispConfig* config_ = nullptr;
};

}  // namespace wisp
