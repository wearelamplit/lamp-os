#pragma once

#include <Arduino.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "config/config.hpp"
#include "expressions/expression_invocation.hpp"
#include "espnow_link.hpp"
#include "lamp_protocol.hpp"
#include "nearby_lamps.hpp"
#include "util/color.hpp"
#include "../firmware/firmware_receiver.hpp"  // FirmwareTransport interface

#ifndef LAMP_ESPNOW_CHANNEL
// ESP-NOW channel; must match the wisp's mesh_link.hpp or the mesh won't form.
#define LAMP_ESPNOW_CHANNEL 6
#endif

// HELLO broadcast interval. Higher = less baseline mesh traffic; the
// fleet-scale airtime budget (HELLO gossip-relays across N lamps) drove
// this to 5s. Must stay well under LAMP_PRUNE_TIME_MS (120s) so peers
// aren't pruned between emits.
#define LAMP_HELLO_INTERVAL_MS 5000

// Pin so a drift back below 5s can't silently re-introduce the airtime
// pressure; bumping it down requires re-validating the fleet-size airtime
// budget.
static_assert(LAMP_HELLO_INTERVAL_MS == 5000,
              "LAMP_HELLO_INTERVAL_MS lock-in for v0x03 (5s baseline). "
              "Bumping back below 5000 must come with a re-validation "
              "against fleet-size airtime budget.");

namespace lamp {

// Called from the loop task when a MSG_CONTROL_OP arrives addressed to this
// lamp (or broadcast). Payload is JSON; caller is expected to parse `char`
// and route to the matching local postPending* function. `srcMac` is the
// sender's WiFi STA MAC (6 bytes; used by the receiver-side cascade
// coalesce so spam from one sender collapses while genuinely concurrent
// cascades from different senders both land). Pointers are only valid
// during the call.
using ControlOpHandler = std::function<void(const uint8_t* payload, size_t len,
                                            const uint8_t srcMac[6])>;

// POD-by-construction so PendingTypedSlot<T>'s portMUX-protected memcpy
// post/drain has well-defined semantics across the WiFi-task -> loop-task
// hand-off. ShowReceiver::handleRecv populates these on the WiFi task
// (Core 0); standard_lamp's loop drain reads them on Core 1 and dispatches
// into the ColorOverride / BrightnessOverride / NearbyLamps modules.
//
// Colors use the Color struct directly (4 bytes/pixel, RGBW) since the loop
// drain hands them to ColorOverride::apply which expects `const Color*`.

struct PendingOverrideColors {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint32_t fadeDurationMs;
  uint8_t numColors;
  Color colors[lamp_protocol::kMaxOverrideColorsPerFrame];
};

struct PendingRestoreColors {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint32_t fadeDurationMs;
};

struct PendingOverrideBrightness {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint16_t fadeDurationMs;
  uint8_t brightness;
};

struct PendingRestoreBrightness {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint16_t fadeDurationMs;
};

struct PendingWispHello {
  uint8_t sourceMac[6];
  uint32_t wispVersion;
  uint8_t flags;
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN];
  char carriedFwChannel[lamp_protocol::WISP_HELLO_FW_CHANNEL_LEN];
  uint32_t carriedFwVersion;
};

// MSG_WISP_PALETTE pending slot. Holds the wisp's broadcast manualPalette
// until the Core 1 drain forwards it into NearbyLamps::cacheWispPalette.
// Sized to kMaxWispPaletteColors * 3 = 150 bytes (matches the on-wire cap).
struct PendingWispPalette {
  uint8_t sourceMac[6];
  uint8_t count;  // 0..kMaxWispPaletteColors
  uint8_t rgb[lamp_protocol::kMaxWispPaletteColors *
              lamp_protocol::WISP_PALETTE_ENTRY_SIZE];
};

// MSG_WISP_CLAIM pending slot. Holds the wisp's claimed-lamp roster until
// the Core 1 drain forwards it into NearbyLamps::cacheWispClaim.
struct PendingWispClaim {
  uint8_t sourceMac[6];
  uint8_t count;
  uint8_t lampMacs[lamp_protocol::kMaxWispClaimEntries][6];
};

// MSG_EVENT pending slot. ShowReceiver's WiFi-task recv path does the
// stagger-list lookup (own MAC -> delayMs) and memcpys the result here;
// the Core 1 drain calls ExpressionManager::tryHandleExpressionEvent
// (JSON parse + cascade-config check + dedup + trigger). Buffer sized to
// maxEventPayloadFor(0) = 234, the best-case payload with no stagger
// entries. Lower stagger counts get larger payloads, and the slot must
// hold whatever the parser accepted or frames get silently dropped at the
// recv-side memcpy boundary.
struct PendingEvent {
  uint8_t  sourceMac[6];
  uint16_t delayMs;          // already resolved by recv-side stagger lookup
  uint16_t payloadLen;
  uint8_t  payload[lamp_protocol::maxEventPayloadFor(0)];
};

// Forwarders implemented in lamp.cpp. ShowReceiver's WiFi-task recv path
// calls these; they own posting into the loop-task pending slots so
// handleRecv stays a thin parse-and-route layer with no knowledge of which
// slot a message type lands in.
void postPendingOverrideColors(const PendingOverrideColors& src);
void postPendingRestoreColors(const PendingRestoreColors& src);
void postPendingOverrideBrightness(const PendingOverrideBrightness& src);
void postPendingRestoreBrightness(const PendingRestoreBrightness& src);
void postPendingWispHello(const PendingWispHello& src);
void postPendingWispPalette(const PendingWispPalette& src);
void postPendingWispClaim(const PendingWispClaim& src);
void postPendingEvent(const PendingEvent& src);

// Forward decl; full type lives in components/firmware/firmware_receiver.hpp.
// ShowReceiver only needs the pointer + the chunk handler, which it calls
// directly on the WiFi task (no slot indirection for the high-frequency
// chunk path).
class FirmwareReceiver;
class FirmwareDistributor;

// Receives HELLO + CONTROL_OP frames over ESP-NOW, and announces this
// lamp's presence (HELLO) so peers can populate their registry with its
// MAC + name + colors. Maintains the grid peer list (incoming HELLOs)
// and dispatches MSG_CONTROL_OP via the registered handler.
//
// Recv runs on the Wi-Fi task; the DedupRing instances guard themselves
// with portMUX internally so the Arduino loop task can call sendControlOp
// concurrently without racing the recv path.
class ShowReceiver {
 public:
  // `cfg` is used to read the lamp's friendly name and current configured
  // shade/base colors at HELLO time. Caller retains ownership.
  void begin(Config* cfg);

  // Called from the Arduino loop task. Emits HELLO at LAMP_HELLO_INTERVAL_MS
  // cadence; otherwise cheap to call every frame.
  void tick();

  // Read this lamp's own MAC. Populated after begin().
  void getMyMac(uint8_t out[6]) const;

  // Register a handler for MSG_CONTROL_OP addressed to this lamp or
  // broadcast. Called on the WiFi recv task; handler must be fast and
  // non-blocking (typically posts to a pending slot).
  void setControlOpHandler(ControlOpHandler h);

  // Broadcast a CONTROL_OP frame onto the grid. Used by the BLE
  // CHAR_REMOTE_OP drain to forward a write to a far lamp.
  bool sendControlOp(const uint8_t targetMac[6], const uint8_t* payload,
                     size_t payloadLen);

  // Broadcast a raw pre-built ESP-NOW frame onto the grid. Used by the
  // MSG_EVENT cascade path which builds the frame in ExpressionManager.
  // Caller is responsible for size limits.
  bool broadcastRaw(const uint8_t* data, size_t len);

  // Allocate the next outbound event sequence number. The cascade path
  // emits two back-to-back copies of the same MSG_EVENT for broadcast-loss
  // resilience; both share the same seq so receivers' eventDedup_
  // collapses the second copy after applying the first.
  uint16_t nextEventSeq();

  // Wire a FirmwareReceiver into the dispatch ladder. handleRecv calls
  // its handleChunkOnRecvTask directly on the WiFi task (Core 0) for
  // MSG_FW_CHUNK; OFFER and DONE go through the PendingFirmwareControl
  // slot to be drained on Core 1. Set BEFORE begin(); the recv callback
  // registers inside begin().
  void setFirmwareReceiver(FirmwareReceiver* r) { firmwareReceiver_ = r; }

  // Wire a FirmwareDistributor into the dispatch ladder. handleRecv calls
  // its onAcceptOnRecvTask / onReqOnRecvTask / onResultOnRecvTask on the
  // WiFi task (Core 0) for MSG_FW_ACCEPT / MSG_FW_REQ / MSG_FW_RESULT
  // addressed to this lamp's MAC. Set BEFORE begin(); same lifecycle as
  // setFirmwareReceiver.
  void setFirmwareDistributor(FirmwareDistributor* d) { firmwareDistributor_ = d; }

  // True if either receive- or send-side OTA is mid-flight. Used by mesh
  // emit sites (HELLO tick, cascade broadcast, override forwards) to
  // suppress non-OTA traffic during gossip OTA, freeing channel airtime
  // for the chunk stream. Inbound dispatch is NOT gated; receiving is
  // always safe.
  bool isOtaInProgress() const;

  // Static recv glue (EspNowLink hands back a C function pointer).
  static ShowReceiver* s_instance;
  static void onRecv(const uint8_t* mac, const uint8_t* data, size_t len,
                     int8_t rssi);

 private:
  EspNowLink link_;
  Config* config_ = nullptr;
  uint8_t myMac_[6] = {0};

  lamp_protocol::DedupRing helloDedup_;
  lamp_protocol::DedupRing controlOpDedup_;
  // Per-type dedup. Each new MSG_* gets its own ring so a
  // CONTROL_OP seq doesn't accidentally suppress an OVERRIDE_COLORS seq
  // from the same sender (seqs are independent per type).
  lamp_protocol::DedupRing overrideColorsDedup_;
  lamp_protocol::DedupRing restoreColorsDedup_;
  lamp_protocol::DedupRing overrideBrightnessDedup_;
  lamp_protocol::DedupRing restoreBrightnessDedup_;
  lamp_protocol::DedupRing wispHelloDedup_;
  // Wisp-to-wisp claim broadcasts. Dedup so a relayed echo can't
  // repeat-fire. Lamps don't act on MSG_WISP_CLAIM (pure pass-through).
  lamp_protocol::DedupRing wispClaimDedup_;
  // Wisp manualPalette broadcasts. Dedup + gossip-relay like wispHello.
  // Lamps DO act on the payload: cache it for the app to read via
  // CHAR_WISP_STATUS.
  lamp_protocol::DedupRing wispPaletteDedup_;
  lamp_protocol::DedupRing eventDedup_;
  // Single shared dedup for the MSG_FW_* family. One sender owns all
  // outbound FW seqs; the 6 msgTypes share one seq counter so cross-msgType
  // collisions can't happen. 64 slots is ample for a single in-flight OTA.
  lamp_protocol::DedupRing firmwareDedup_;

  uint32_t lastHelloMs_ = 0;
  uint16_t helloSeq_ = 0;
  uint16_t controlOpSeq_ = 0;
  uint16_t eventSeq_ = 0;

  ControlOpHandler controlOpHandler_;
  FirmwareReceiver*    firmwareReceiver_    = nullptr;
  FirmwareDistributor* firmwareDistributor_ = nullptr;

  void handleRecv(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
  void emitHello();
};

// FirmwareTransport adapter for the ESP-NOW mesh path. Thin wrapper over
// ShowReceiver for the wisp-driven OTA flow: the lamp accepts MSG_FW_OFFER
// over the mesh and emits ACCEPT/REQ/RESULT the same way. The BLE-driven
// flow uses the sibling `BleFirmwareTransport` (ble_control.hpp) that
// notifies on CHAR_FW_STATUS.
class EspNowFirmwareTransport : public FirmwareTransport {
 public:
  explicit EspNowFirmwareTransport(ShowReceiver* link) : link_(link) {}
  void getMyMac(uint8_t out[6]) const override { link_->getMyMac(out); }
  bool sendFrame(const uint8_t* data, size_t len) override {
    return link_->broadcastRaw(data, len);
  }
 private:
  ShowReceiver* link_;
};

}  // namespace lamp
