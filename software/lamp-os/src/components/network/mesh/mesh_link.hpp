#pragma once

#include <Arduino.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "config/config.hpp"
#include "expressions/expression_invocation.hpp"
#include "components/network/transport/espnow_link.hpp"
#include "components/network/protocol/lamp_protocol.hpp"
#include "hello_interval.hpp"
#include "lamp_roster.hpp"
#include "pending_slots.hpp"
#include "util/color.hpp"
#include "components/firmware/firmware_receiver.hpp"  // FirmwareTransport interface

#ifndef LAMP_ESPNOW_CHANNEL
// ESP-NOW channel; must match the wisp's mesh_link.hpp or the mesh won't form.
#define LAMP_ESPNOW_CHANNEL 6
#endif

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

// The Pending* Core0→Core1 hand-off DTOs live in pending_slots.hpp (included
// above) so the pending-slot aggregate can pull the payload shapes without
// the whole transport.

// Forwarders implemented in lamp.cpp. MeshLink's WiFi-task recv path
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
void postPendingWispPaint(const PendingWispPaint& src);
void postPendingCommand(const PendingCommand& src);
void postPendingEvent(const PendingEvent& src);
void postPendingColorQuery(const PendingColorQuery& src);
void postPendingColorInfo(const PendingColorInfo& src);

// Forward decl; full type lives in components/firmware/firmware_receiver.hpp.
// MeshLink only needs the pointer + the chunk handler, which it calls
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
class MeshLink {
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

  // Broadcast a MSG_COMMAND frame targeting a specific nearby lamp.
  // `invocationJson` is the ExpressionInvocation JSON; `len` must be
  // 1..COMMAND_MAX_PAYLOAD. Deduped so a loop-back broadcast doesn't
  // re-trigger locally.
  bool sendCommand(const uint8_t targetMac[6], const uint8_t* invocationJson,
                   size_t len);

  // Broadcast a MSG_EVENT frame; payload is the ExpressionInvocation JSON.
  bool sendEvent(const uint8_t* payloadJson, size_t len);

  bool sendColorQuery(const uint8_t targetMac[6]);
  bool sendColorInfo(const uint8_t targetMac[6],
                     const uint8_t* baseStops, uint8_t baseCount,
                     const uint8_t* shadeStops, uint8_t shadeCount);

  // Broadcast a raw pre-built ESP-NOW frame onto the grid. Used by
  // EspNowFirmwareTransport. Caller is responsible for size limits.
  bool broadcastRaw(const uint8_t* data, size_t len);

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
  static MeshLink* s_instance;
  static void onRecv(const uint8_t* mac, const uint8_t* data, size_t len,
                     int8_t rssi);

 private:
  EspNowLink link_;
  Config* config_ = nullptr;
  uint8_t myMac_[6] = {0};

  // Capacity per ring is sized to the message type's traffic. Relay-heavy
  // every-lamp types get the full 64; single-hop / low-rate types get less.
  lamp_protocol::DedupRing<64> helloDedup_;
  lamp_protocol::DedupRing<64> controlOpDedup_;
  // Per-type dedup. Each new MSG_* gets its own ring so a
  // CONTROL_OP seq doesn't accidentally suppress an OVERRIDE_COLORS seq
  // from the same sender (seqs are independent per type).
  lamp_protocol::DedupRing<16> overrideColorsDedup_;
  lamp_protocol::DedupRing<16> restoreColorsDedup_;
  lamp_protocol::DedupRing<16> overrideBrightnessDedup_;
  lamp_protocol::DedupRing<16> restoreBrightnessDedup_;
  lamp_protocol::DedupRing<32> wispHelloDedup_;
  // Wisp claim broadcasts. No relay; dedup prevents repeat-fire on direct
  // reception. Entries accumulate in LampRoster's fleet cache for
  // CHAR_WISP_CLAIMS and display-slot admission.
  lamp_protocol::DedupRing<16> wispClaimDedup_;
  // Wisp manualPalette broadcasts. Dedup + gossip-relay like wispHello.
  // Lamps DO act on the payload: cache it for the app to read via
  // CHAR_WISP_STATUS.
  lamp_protocol::DedupRing<32> wispPaletteDedup_;
  // Per-lamp paint colors from the wisp. No relay; dedup prevents
  // repeat-fire. Cached for CHAR_WISP_CLAIMS to serve the app's
  // painted-lamps preview.
  lamp_protocol::DedupRing<16> wispPaintDedup_;
  lamp_protocol::DedupRing<64> commandDedup_;
  lamp_protocol::DedupRing<16> eventDedup_;
  lamp_protocol::DedupRing<16> colorQueryDedup_;
  lamp_protocol::DedupRing<16> colorInfoDedup_;
  // Single shared dedup for the MSG_FW_* family. One sender owns all
  // outbound FW seqs; the 6 msgTypes share one seq counter so cross-msgType
  // collisions can't happen. 16 slots is ample for a single in-flight OTA.
  lamp_protocol::DedupRing<16> firmwareDedup_;

  uint32_t lastHelloMs_ = 0;
  uint16_t helloSeq_ = 0;
  uint16_t controlOpSeq_ = 0;
  uint16_t commandSeq_    = 0;
  uint16_t eventSeq_      = 0;
  uint16_t colorQuerySeq_ = 0;
  uint16_t colorInfoSeq_  = 0;

  ControlOpHandler controlOpHandler_;
  FirmwareReceiver*    firmwareReceiver_    = nullptr;
  FirmwareDistributor* firmwareDistributor_ = nullptr;

  void handleRecv(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
  void emitHello();
};

// FirmwareTransport adapter for the ESP-NOW mesh path. Thin wrapper over
// MeshLink for the wisp-driven OTA flow: the lamp accepts MSG_FW_OFFER
// over the mesh and emits ACCEPT/REQ/RESULT the same way. The BLE-driven
// flow uses the sibling `BleFirmwareTransport` (ble_control.hpp) that
// notifies on CHAR_FW_STATUS.
class EspNowFirmwareTransport : public FirmwareTransport {
 public:
  explicit EspNowFirmwareTransport(MeshLink* link) : link_(link) {}
  void getMyMac(uint8_t out[6]) const override { link_->getMyMac(out); }
  bool sendFrame(const uint8_t* data, size_t len) override {
    return link_->broadcastRaw(data, len);
  }
 private:
  MeshLink* link_;
};

}  // namespace lamp
