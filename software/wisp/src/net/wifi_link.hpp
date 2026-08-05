// WifiLink hosts the wisp's softAP for pre-mesh lamps. Brought up once at
// boot on the mesh channel and left up; the ArtNet emitter gates on
// canBroadcast() and pins the egress netif off isAp().
//
// Coex: the mesh pins the radio to LAMP_ESPNOW_CHANNEL and sends ESP-NOW on
// WIFI_IF_STA. The softAP runs in WIFI_AP_STA on the same channel, so the STA
// netif (and the mesh) stays up and same-channel WiFi doesn't disturb ESP-NOW.

#pragma once

#include <Arduino.h>
#include <IPAddress.h>

#include <atomic>
#include <functional>
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
  // Clears any channel-mismatch guard and re-enables auto-reconnect. After a
  // mismatch the wisp does not retry on its own; recovery is an explicit
  // reconnect (a fresh setWifi or the wifiReconnect op).
  void reconnect();

  // Drains a deferred STA_GOT_IP verdict on the loop task. The GOT_IP event
  // only records the associated channel; the heavy radio work (drop the
  // wrong-channel association, re-pin the radio to the mesh channel) runs
  // here to keep the WiFi event task out of re-entrant radio calls.
  void loop();

  // softAP role on the mesh channel (WIFI_AP_STA so ESP-NOW keeps working).
  // ssid/pass are the wisp's own network, not the WispConfig STA creds.
  // Idempotent: a no-op when the AP is already up.
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

  // True once an AP association landed on a channel other than the mesh
  // channel and the guard dropped it. Cleared by reconnect() or a clean
  // mesh-channel association. apChannel() is the offending channel (0 when
  // no mismatch). Loop-task state, read by the status emitter.
  bool channelMismatch() const { return channelMismatch_; }
  int apChannel() const { return lastStaChannel_; }

  // Fired from loop() only when channelMismatch() flips, so the status emitter
  // can re-emit at once instead of waiting for the next heartbeat.
  void setOnChangeCallback(std::function<void()> cb) { onChange_ = std::move(cb); }

  // SSID/password reflected from the bound WispConfig. Returns empty
  // strings before begin() is called or when the store has no creds.
  std::string ssid() const;
  std::string password() const;

 private:
  WispConfig* config_ = nullptr;
  bool started_ = false;
  Mode mode_ = Mode::Off;
  bool apUp_ = false;

  // Channel carried by a STA_GOT_IP event, handed from the WiFi event task to
  // loop(). 0 means nothing pending; loop() exchanges it back to 0.
  std::atomic<int> pendingStaChannel_{0};
  bool channelMismatch_ = false;
  int lastStaChannel_ = 0;

  std::function<void()> onChange_;
};

}  // namespace wisp
