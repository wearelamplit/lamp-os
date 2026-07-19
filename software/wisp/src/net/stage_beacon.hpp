// StageBeacon is a non-connectable BLE advertisement carrying WiFi creds for
// pre-mesh lamps to discover and auto-join.
//
// Magic ID 42007. Payload: <2 bytes magic LE><ssid\0><password\0>.
// Max combined ssid+password+2 NULs is 26 bytes. Caller must enforce
// or advertising is refused.

#pragma once

#include <string>

namespace wisp {

class WispConfig;

class StageBeacon {
 public:
  // Idempotent after first call; config binding is fixed at first begin().
  void begin(const std::string& deviceName, WispConfig* config);

  // Re-read creds from WispConfig and (re)start the advert. Called by
  // WispOpDispatcher after a setWifi op persists new creds, so a connected
  // operator can swap the deployment SSID and watch pre-mesh lamps follow.
  // If creds are empty, advertising stops. Safe to call any time after
  // begin(); a no-op (with log) before begin().
  void refreshAdvert();

  // Advertise explicit creds instead of the WispConfig STA creds, so Manual
  // mode points pre-mesh lamps at the wisp's own softAP. Same byte budget.
  void advertiseCreds(const std::string& ssid, const std::string& password);

  // Stop advertising. Safe to call before begin().
  void stop();

  bool isAdvertising() const { return advertising_; }

 private:
  void startAdvert(const std::string& ssid, const std::string& password);

  bool initialized_ = false;
  bool advertising_ = false;
  std::string deviceName_;
  WispConfig* config_ = nullptr;
};

}  // namespace wisp
