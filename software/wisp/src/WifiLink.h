// WifiLink — STA-mode bring-up backed by WispConfig credentials.
//
// Owns nothing exotic: just the small subset of WiFi state the wisp needs
// (connection state via Arduino's WiFi.h) and a back-reference to the
// shared WispConfig source-of-truth for SSID/password.
//
// History: an earlier draft kept its own NVS namespace ("wisp_wifi") with
// `Preferences` so the artnet-bridge could come up without WispConfig. That
// duplicated the WispConfig setWifi-op storage, leaving two namespaces with
// the same conceptual data. The wisp now has exactly one credentials store
// (WispConfig's "wisp" namespace, keys wifiSsid / wifiPw) — both WifiLink
// and StageBeacon read from it.
//
// Coex caveat (read once): MeshLink::begin() calls esp_wifi_set_channel
// (LAMP_ESPNOW_CHANNEL = 11 as of 2026-06-10) before init'ing ESP-NOW.
// Once we associate to an AP here, the radio switches to whatever
// channel the AP picked. Mesh peers pinned to channel 11 won't see
// broadcasts unless the venue AP is also on channel 11. See
// docs/mesh-deployment.md "channel coex" — same constraint Aurora faces.

#pragma once

#include <Arduino.h>

#include <string>

namespace wisp {

class WispConfig;

class WifiLink {
 public:
  // Bind to the shared WispConfig store and, if persisted creds exist,
  // kick a WiFi.begin(). Idempotent — safe to call once during setup().
  // The config reference must outlive this WifiLink (lifetime is process
  // lifetime in practice; main.cpp owns both as globals).
  void begin(WispConfig* config);

  // Re-read creds from WispConfig, drop any existing association, and
  // attempt a fresh WiFi.begin(). Called by WispOpDispatcher after a
  // setWifi op persists new creds into WispConfig. Idempotent and safe
  // to call repeatedly; if creds are empty the call no-ops with a log
  // line.
  void reconnect();

  // Snapshot accessors. Cheap. Callable from any task.
  bool isConnected() const;
  // SSID/password reflected from the bound WispConfig. Returns empty
  // strings before begin() is called or when the store has no creds.
  std::string ssid() const;
  std::string password() const;

 private:
  WispConfig* config_ = nullptr;
  bool started_ = false;
};

}  // namespace wisp
