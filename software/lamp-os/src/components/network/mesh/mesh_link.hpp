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
#include "hello_relay_suppressor.hpp"
#include "lamp_roster.hpp"
#include "resend_ring.hpp"
#include "pending_slots.hpp"
#include "wisp_coex.hpp"
#include "wisp_state.hpp"
#include "meshmix.hpp"
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
void postPendingWispState(const PendingWispState& src);
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

  // Unicast a CONTROL_OP to `targetMac` (the in-range display wisp). Arms a
  // spaced unicast resend: the wisp's bursty RX-scan can drop every MAC-ARQ
  // attempt inside one scan gap, so the frame is re-sent kResends times, each
  // copy also unicast (individually MAC-acked) to the same MAC and collapsed
  // by the receiver's dedup. Records the seq in controlOpDedup_. Returns the
  // initial link send result.
  bool sendControlOpUnicast(const uint8_t targetMac[6], const uint8_t* payload,
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
  // Defers each first-seen HELLO relay; drops it if enough neighbors already
  // relayed the same (mac, seq). Bounds HELLO airtime below the ~N^2 that
  // relaying every first sight costs. Receive-side only, no wire change.
  HelloRelaySuppressor helloSuppressor_;
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
  // Wisp manualPalette broadcasts. Adopted only when heard directly (no
  // relay). Lamps DO act on the payload: cache it for the app to read via
  // CHAR_WISP_STATUS.
  lamp_protocol::DedupRing<32> wispPaletteDedup_;
  // Per-lamp paint colors from the wisp. No relay; dedup prevents
  // repeat-fire. Cached for CHAR_WISP_CLAIMS to serve the app's
  // painted-lamps preview.
  lamp_protocol::DedupRing<16> wispPaintDedup_;
  // Declarative per-lamp state from the wisp. No relay; dedup prevents
  // repeat-fire on direct reception.
  lamp_protocol::DedupRing<16> wispStateDedup_;
  lamp_protocol::DedupRing<64> commandDedup_;
  // Greeting/expression low-rate types. 32 slots (up from 16) so a dense
  // greet-storm plus the resend copies below don't evict a still-live
  // (mac, seq) before its duplicate lands.
  lamp_protocol::DedupRing<32> eventDedup_;
  lamp_protocol::DedupRing<32> colorQueryDedup_;
  lamp_protocol::DedupRing<32> colorInfoDedup_;
  // Single shared dedup for the MSG_FW_* family. One sender owns all
  // outbound FW seqs; the 6 msgTypes share one seq counter so cross-msgType
  // collisions can't happen. 16 slots is ample for a single in-flight OTA.
  lamp_protocol::DedupRing<16> firmwareDedup_;

  // WISP_HELLO seq-gap coex meter. Runs on the recv task (handleRecv),
  // never the loop task; see qa/coex.md.
#ifdef LAMP_DEBUG
  WispCoexMeter wispCoexMeter_;
  WispStateMeter wispStateMeter_;
  MeshMix meshMix_;
#endif

  uint32_t lastHelloMs_ = 0;
  // MAC-seeded first-HELLO offset so a fleet powering on together doesn't
  // boot-burst in lockstep. Applied once, to the first emit only.
  uint32_t helloBootPhaseMs_ = 0;
  uint8_t prevOtaState_ = lamp_protocol::kOtaStateIdle;
  uint16_t helloSeq_ = 0;
  uint16_t controlOpSeq_ = 0;

  // Spaced re-broadcast of the unacked types that must survive the C6's
  // bursty RX-scan gaps. Per-type instances (not one shared ring) so a
  // command fan-out's replays don't crowd out a control-op's. The ring
  // covers a 384 B command frame (358 B payload budget); a larger frame
  // sends once. See docs/dev/networking.md.
  static constexpr size_t kCommandResendPayloadMax = 358;
  static constexpr size_t kCommandResendMax =
      lamp_protocol::COMMAND_FIXED_SIZE + kCommandResendPayloadMax +
      lamp_protocol::COMMAND_TAG_SIZE;
  ResendRing<lamp_protocol::CONTROL_MAX_SIZE, 1> controlOpResend_;
  ResendRing<lamp_protocol::CONTROL_MAX_SIZE, 1> controlOpUnicastResend_;
  ResendRing<kCommandResendMax, 10> commandResend_;
  ResendRing<lamp_protocol::COLOR_QUERY_SIZE, 1> colorQueryResend_;
  ResendRing<lamp_protocol::COLOR_INFO_MAX_SIZE, 4> colorInfoResend_;
  uint16_t commandSeq_    = 0;
  uint16_t eventSeq_      = 0;
  uint16_t colorQuerySeq_ = 0;
  uint16_t colorInfoSeq_  = 0;

  ControlOpHandler controlOpHandler_;
  FirmwareReceiver*    firmwareReceiver_    = nullptr;
  FirmwareDistributor* firmwareDistributor_ = nullptr;

  void handleRecv(const uint8_t* mac, const uint8_t* data, size_t len, int8_t rssi);
  void emitHello(uint8_t otaState);
  // Receiver in progress takes priority over distributor. kOtaStateIdle when
  // neither is active.
  uint8_t currentOtaState() const;
#ifdef LAMP_DEBUG
  void reportWispCoex(const uint8_t mac[6], uint32_t nowMs);
  void reportWispState(const uint8_t sourceMac[6],
                       const lamp_protocol::ParsedWispState& ws, uint32_t nowMs);
  void reportMeshMix(uint32_t nowMs);
#endif

  // Re-broadcast a received frame for gossip relay.
  void relay(const uint8_t* data, size_t len) {
    link_.broadcast(data, len);
#ifdef LAMP_DEBUG
    meshMix_.relayedOut++;
#endif
  }
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
