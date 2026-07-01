// WifiLink — WiFi role that follows the source mode. STA joins the external AP
// whose creds live in the shared WispConfig store (Aurora); softAP hosts the
// wisp's own network on the mesh channel (Manual). The ArtNet emitter gates on
// canBroadcast() and targets broadcastIp().
//
// Coex: the mesh pins the radio to LAMP_ESPNOW_CHANNEL and sends ESP-NOW on
// WIFI_IF_STA. softAP runs in WIFI_AP_STA on the same channel so the STA netif
// (and the mesh) stays up. STA association to an external AP moves the radio to
// that AP's channel, so mesh peers pinned to channel 11 only see broadcasts
// when the venue AP is also on 11.

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

  // STA role: drop any softAP, re-read WispConfig creds, reconnect.
  void startSta();

  // softAP role on the mesh channel (WIFI_AP_STA so ESP-NOW keeps working).
  // ssid/pass are the wisp's own network, not the WispConfig STA creds.
  void startSoftAp(const char* ssid, const char* pass);

  // Drop any softAP and STA association; keep the radio pinned to the mesh
  // channel so ESP-NOW survives. canBroadcast() reads false afterwards.
  void stop();

  // True when ArtNet frames can egress: STA associated, or softAP up.
  bool canBroadcast() const;
  // Broadcast target for the active role: STA subnet directed-broadcast is the
  // global 255.255.255.255; AP uses the softAP subnet broadcast so frames
  // reach lamps joined to the wisp's own netif.
  IPAddress broadcastIp() const;

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
