// WifiLink — hosts the wisp's softAP for pre-mesh lamps. Brought up once at
// boot on the mesh channel and left up; the ArtNet emitter gates on
// canBroadcast() and pins the egress netif off isAp().
//
// Coex: the mesh pins the radio to LAMP_ESPNOW_CHANNEL and sends ESP-NOW on
// WIFI_IF_STA. The softAP runs in WIFI_AP_STA on the same channel, so the STA
// netif (and the mesh) stays up and same-channel WiFi doesn't disturb ESP-NOW.

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <string>

namespace wisp {

class WispConfig;

class WifiLink {
 public:
  enum class Mode : uint8_t { Off, Sta, Ap };

  // Bind to WispConfig and, if persisted creds exist, kick a WiFi.begin().
  // Idempotent. config must outlive this WifiLink (main.cpp owns both).
  void begin(WispConfig* config);

  // Re-read creds, drop any association, attempt a fresh WiFi.begin(). Called
  // after a setWifi op persists new creds. No-ops (with a log) if creds empty.
  void reconnect();

  // softAP role on the mesh channel (WIFI_AP_STA so ESP-NOW keeps working).
  // ssid/pass are the wisp's own network, not the WispConfig STA creds.
  void startSoftAp(const char* ssid, const char* pass);

  // True when ArtNet frames can egress: STA associated, or softAP up.
  bool canBroadcast() const;
  // A UDP send left to default routing in WIFI_AP_STA can egress the wrong
  // netif, so ArtNet pins the interface per role off this.
  bool isAp() const { return mode_ == Mode::Ap; }

  // Fill out[] with the IPs of stations currently joined to the softAP (DHCP
  // leases), returning the count. Zero outside AP mode. ArtNet unicasts to each
  // because WiFi broadcast is unreliable to sleepy lamps.
  size_t apClientIps(IPAddress* out, size_t maxOut) const;

  // Snapshot accessors. Cheap. Callable from any task.
  bool isConnected() const;
  // SSID/password reflected from the bound WispConfig. Returns empty
  // strings before begin() is called or when the store has no creds.
  std::string ssid() const;
  std::string password() const;

 private:
  WispConfig* config_ = nullptr;
  bool started_ = false;
  Mode mode_ = Mode::Off;
  bool apUp_ = false;
};

}  // namespace wisp
