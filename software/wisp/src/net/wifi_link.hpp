// WifiLink — STA-mode bring-up backed by WispConfig credentials. Reads
// SSID/password from the shared WispConfig store (the wisp's single creds
// store; StageBeacon reads the same).
//
// Coex caveat: MeshLink::begin() pins the radio to LAMP_ESPNOW_CHANNEL before
// starting ESP-NOW. Associating to an AP here switches the radio to the AP's
// channel, so mesh peers pinned to channel 11 won't see broadcasts unless the
// venue AP is also on 11.

#pragma once

#include <Arduino.h>

#include <string>

namespace wisp {

class WispConfig;

class WifiLink {
 public:
  // Bind to WispConfig and, if persisted creds exist, kick a WiFi.begin().
  // Idempotent. config must outlive this WifiLink (main.cpp owns both).
  void begin(WispConfig* config);

  // Re-read creds, drop any association, attempt a fresh WiFi.begin(). Called
  // after a setWifi op persists new creds. No-ops (with a log) if creds empty.
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
