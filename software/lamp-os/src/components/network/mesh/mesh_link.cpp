#include "mesh_link.hpp"

#include <Arduino.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/fs_ota.hpp"
#include "components/network/mesh/proximity.hpp"
#include "components/network/protocol/command_auth.hpp"
#include "version.hpp"

#include <lampos/blended_identity.hpp>

namespace lamp {

namespace {
// The unacked broadcast types have no per-frame retry, and per-frame loss on
// a coex-contended link runs ~0.65 (correlated, not independent). 2 extra
// copies (3 total) spaced kResendGapMs apart give each receiver one copy per
// scan-listen gap; the gap must exceed the ~15 ms scan window so the copies
// don't share a hole. Dedup collapses the copies to one apply.
constexpr uint8_t kResends = 2;
constexpr uint32_t kResendGapMs = 40;

// Outbound bid rate limit (any bidType). Bench-tunable; keep in sync with the
// mirror in test_msg_bid.
constexpr uint32_t kBidSendCooldownMs = 3000;
}  // namespace

MeshLink* MeshLink::s_instance = nullptr;

void MeshLink::onRecv(const uint8_t* mac, const uint8_t* data, size_t len,
                          int8_t rssi) {
  if (s_instance) s_instance->handleRecv(mac, data, len, rssi);
}

void MeshLink::begin(Config* cfg) {
  config_ = cfg;
  s_instance = this;
  // Force init before the recv callback registers or any send runs, so the
  // lazy path never writes s_key concurrently across the recv/send tasks.
  lamp_protocol::command_auth::init();
  if (!link_.begin(&MeshLink::onRecv)) {
    Serial.println("[show] ESP-NOW init failed");
    return;
  }
  link_.getMac(myMac_);
  // Random boot seq: a fixed start replays (mac, seq) tuples that peers still
  // hold in their HELLO dedup rings after a quick reboot, so the fresh HELLOs
  // drop before the roster's version/state update and the peer stalls stale.
  helloSeq_ = static_cast<uint16_t>(esp_random());
  helloBootPhaseMs_ =
      ((uint32_t(myMac_[4]) << 8) | myMac_[5]) % LAMP_HELLO_BURST_INTERVAL_MS;
  Serial.printf("[show] ready, mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                myMac_[0], myMac_[1], myMac_[2], myMac_[3], myMac_[4], myMac_[5]);
}

void MeshLink::tick() {
  uint32_t now = millis();
  auto send = [this](const uint8_t* frame, size_t len) {
    link_.broadcast(frame, len);
  };
  controlOpResend_.service(now, send);
  controlOpUnicastResend_.service(now, [this](const uint8_t* frame, size_t len) {
    link_.send(&frame[lamp_protocol::HEADER_SIZE], frame, len);
  });
  commandResend_.service(now, send);
  colorQueryResend_.service(now, send);
  colorInfoResend_.service(now, send);
  helloSuppressor_.tick(now, [this](const uint8_t* frame, size_t len) {
    relay(frame, len);
  });
  const uint8_t otaState = currentOtaState();
  uint8_t sendingTo[6];
  const bool hasSendingTo =
      otaState == lamp_protocol::kOtaStateSending &&
      firmwareDistributor_ && firmwareDistributor_->getPeerMac(sendingTo);
  if (config_) {
    config_->setLampOtaState(otaState, hasSendingTo ? sendingTo : nullptr);
  }
  // Force an immediate HELLO on any otaState change so sending/idle reaches
  // peers within a tick, too fast for the steady interval given a ~60s OTA.
  if (otaState != prevOtaState_) {
    prevOtaState_ = otaState;
    lastHelloMs_ = 0;
  }
  if (lastHelloMs_ == 0 && now < helloBootPhaseMs_) return;
  if (now - lastHelloMs_ < helloIntervalMs(now) && lastHelloMs_ != 0) return;
  lastHelloMs_ = now;
  // Only a receiver goes silent; it needs the airtime for the chunk stream. A
  // pure distributor keeps emitting so its otaState=Sending reaches peers.
  if (firmwareReceiver_ && firmwareReceiver_->isInProgress()) return;
  emitHello(otaState);
}

uint8_t MeshLink::currentOtaState() const {
  if (firmwareReceiver_ && firmwareReceiver_->isInProgress())
    return lamp_protocol::kOtaStateReceiving;
  if (firmwareDistributor_ && firmwareDistributor_->isInProgress())
    return lamp_protocol::kOtaStateSending;
  return lamp_protocol::kOtaStateIdle;
}

bool MeshLink::isOtaInProgress() const {
  const bool rx = firmwareReceiver_ ? firmwareReceiver_->isInProgress() : false;
  const bool tx = firmwareDistributor_ ? firmwareDistributor_->isInProgress() : false;
  return rx || tx;
}

void MeshLink::getMyMac(uint8_t out[6]) const {
  std::memcpy(out, myMac_, 6);
}

void MeshLink::setControlOpHandler(ControlOpHandler h) {
  controlOpHandler_ = std::move(h);
}

bool MeshLink::sendControlOp(const uint8_t targetMac[6],
                                 const uint8_t* payload, size_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return false;
  uint8_t buf[lamp_protocol::CONTROL_MAX_SIZE];
  // sourceMac is this lamp so peers and the originator can dedup re-broadcasts.
  const size_t n = lamp_protocol::buildControlOp(buf, sizeof(buf), controlOpSeq_++,
                                                 targetMac, myMac_,
                                                 payload, payloadLen);
  if (!n) return false;
  // Record in the dedup ring so the inbound re-broadcast (from a peer)
  // doesn't loop back as an apply-locally.
  controlOpDedup_.record(myMac_, lamp_protocol::MSG_CONTROL_OP, controlOpSeq_ - 1);
  const bool ok = link_.broadcast(buf, n);
  controlOpResend_.enqueue(buf, n, millis(), kResends, kResendGapMs);
  return ok;
}

bool MeshLink::sendControlOpUnicast(const uint8_t targetMac[6],
                                    const uint8_t* payload, size_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return false;
  uint8_t buf[lamp_protocol::CONTROL_MAX_SIZE];
  const size_t n = lamp_protocol::buildControlOp(buf, sizeof(buf), controlOpSeq_++,
                                                 targetMac, myMac_,
                                                 payload, payloadLen);
  if (!n) return false;
  controlOpDedup_.record(myMac_, lamp_protocol::MSG_CONTROL_OP, controlOpSeq_ - 1);
  const bool ok = link_.send(targetMac, buf, n);
  controlOpUnicastResend_.enqueue(buf, n, millis(), kResends, kResendGapMs);
  return ok;
}

bool MeshLink::sendCommand(const uint8_t targetMac[6],
                              const uint8_t* invocationJson, size_t len) {
  if (len == 0 || len > lamp_protocol::COMMAND_MAX_PAYLOAD) return false;
  // static: the ~1470 B frame is too big for the loop-task stack; every
  // sendCommand caller (cascade + greeting) runs on the Core 1 loop task.
  static uint8_t buf[lamp_protocol::COMMAND_FIXED_SIZE + lamp_protocol::COMMAND_MAX_PAYLOAD +
              lamp_protocol::COMMAND_TAG_SIZE];
  const size_t n = lamp_protocol::buildCommand(buf, sizeof(buf), commandSeq_++,
                                               myMac_, targetMac,
                                               invocationJson, len);
  if (!n) return false;
  const size_t framed = lamp_protocol::command_auth::appendTag(buf, n, sizeof(buf));
  commandDedup_.record(myMac_, lamp_protocol::MSG_COMMAND, commandSeq_ - 1);
  const bool ok = link_.broadcast(buf, framed);
  if (!commandResend_.enqueue(buf, framed, millis(), kResends, kResendGapMs)) {
#ifdef LAMP_DEBUG
    Serial.printf("[send] COMMAND resend dropped: frame=%u > ring cap, dst=%02X:%02X:%02X:%02X:%02X:%02X (single send only)\n",
                  (unsigned)framed, targetMac[0], targetMac[1], targetMac[2],
                  targetMac[3], targetMac[4], targetMac[5]);
#endif
  }
  return ok;
}

bool MeshLink::sendEvent(const uint8_t* payloadJson, size_t len) {
  if (len == 0 || len > lamp_protocol::EVENT_MAX_PAYLOAD) return false;
  uint8_t buf[lamp_protocol::EVENT_FIXED_SIZE + lamp_protocol::EVENT_MAX_PAYLOAD +
              lamp_protocol::EVENT_TAG_SIZE];
  const size_t n = lamp_protocol::buildEvent(buf, sizeof(buf), eventSeq_++,
                                             myMac_, payloadJson, len);
  if (!n) return false;
  const size_t framed = lamp_protocol::command_auth::appendTag(buf, n, sizeof(buf));
  eventDedup_.record(myMac_, lamp_protocol::MSG_EVENT, eventSeq_ - 1);
  return link_.broadcast(buf, framed);
}

bool MeshLink::sendBid(uint8_t bidType) {
  const uint32_t now = millis();
  if (lastBidSendMs_ != 0 && (now - lastBidSendMs_) < kBidSendCooldownMs) {
    return false;
  }
  uint8_t buf[lamp_protocol::BID_SIZE];
  const size_t n = lamp_protocol::buildBid(buf, sizeof(buf), bidSeq_++,
                                           myMac_, bidType);
  if (!n) return false;
  const size_t framed = lamp_protocol::command_auth::appendTag(buf, n, sizeof(buf));
  bidDedup_.record(myMac_, lamp_protocol::MSG_BID, bidSeq_ - 1);
  lastBidSendMs_ = now;
  return link_.broadcast(buf, framed);
}

bool MeshLink::sendColorQuery(const uint8_t targetMac[6]) {
  uint8_t buf[lamp_protocol::COLOR_QUERY_SIZE];
  const size_t n = lamp_protocol::buildColorQuery(buf, sizeof(buf),
                                                  colorQuerySeq_++,
                                                  myMac_, targetMac);
  if (!n) return false;
  colorQueryDedup_.record(myMac_, lamp_protocol::MSG_COLOR_QUERY, colorQuerySeq_ - 1);
  const bool ok = link_.broadcast(buf, n);
  colorQueryResend_.enqueue(buf, n, millis(), kResends, kResendGapMs);
  return ok;
}

bool MeshLink::sendColorInfo(const uint8_t targetMac[6],
                             const uint8_t* baseStops, uint8_t baseCount,
                             const uint8_t* shadeStops, uint8_t shadeCount) {
  uint8_t buf[lamp_protocol::COLOR_INFO_MAX_SIZE];
  const size_t n = lamp_protocol::buildColorInfo(buf, sizeof(buf),
                                                 colorInfoSeq_++,
                                                 myMac_, targetMac,
                                                 baseStops, baseCount,
                                                 shadeStops, shadeCount);
  if (!n) return false;
  colorInfoDedup_.record(myMac_, lamp_protocol::MSG_COLOR_INFO, colorInfoSeq_ - 1);
  const bool ok = link_.broadcast(buf, n);
  colorInfoResend_.enqueue(buf, n, millis(), kResends, kResendGapMs);
  return ok;
}

bool MeshLink::broadcastRaw(const uint8_t* data, size_t len) {
  return link_.broadcast(data, len);
}

// Broadcast is all-FF.
static bool addressedToUs(const uint8_t targetMac[6], const uint8_t myMac[6]) {
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return std::memcmp(targetMac, myMac, 6) == 0 ||
         std::memcmp(targetMac, bcast, 6) == 0;
}

#ifdef LAMP_DEBUG
namespace {
constexpr uint32_t kWispCoexEmitIntervalMs = 10000;
constexpr uint32_t kMeshMixWindowMs = 30000;
}  // namespace

// A/B tool for the wisp coex-hole investigation (qa/coex.md): maxgap is the
// presence-stability signal (wall-clock between hellos). No loss%: the wisp
// shares one seq across all its message types, so a per-type seq gap is not
// loss. maxGapMs resets each emit; recv stays cumulative for the boot.
void MeshLink::reportWispCoex(const uint8_t mac[6], uint32_t nowMs) {
  WispCoexSlot& s = wispCoexMeter_.record(mac, nowMs);
  if (nowMs - s.lastEmitMs < kWispCoexEmitIntervalMs) return;
  s.lastEmitMs = nowMs;
  Serial.printf("[wispcoex] wisp=%02X:%02X recv=%u maxgap=%ums\n",
                mac[0], mac[1], (unsigned)s.recv, (unsigned)s.maxGapMs);
  s.maxGapMs = 0;
}

// Control-plane instrument for MSG_WISP_STATE (docs/dev/networking.md):
// recv/adopt/release counts plus this lamp's adopt/release edges, scoped to
// the display wisp so a rival's frame can't flip the state. Windowed line
// emits in reportMeshMix; edges log here on transition only.
void MeshLink::reportWispState(const uint8_t sourceMac[6],
                               const lamp_protocol::ParsedWispState& ws,
                               uint32_t nowMs) {
  uint8_t dispMac[6];
  if (lampRoster.copyDisplayWispMac(dispMac, nowMs) &&
      std::memcmp(dispMac, sourceMac, 6) != 0) {
    return;
  }
  const bool selfPresent =
      lamp_protocol::findWispStateEntry(ws.entries, ws.count, myMac_) != nullptr;
  const WispStateEdge edge = wispStateMeter_.record(selfPresent);
  if (edge == WispStateEdge::kAdopt) {
    Serial.printf("[wispstate] ADOPT src=%02X:%02X seq=%u\n",
                  sourceMac[0], sourceMac[1], (unsigned)ws.seq);
  } else if (edge == WispStateEdge::kRelease) {
    Serial.printf("[wispstate] RELEASE src=%02X:%02X seq=%u\n",
                  sourceMac[0], sourceMac[1], (unsigned)ws.seq);
  }
}

// meshmix airtime instrument (docs/dev/networking.md): once per window, dump
// the received-frame mix and relay count, then reset. windowStartMs seeds on
// the first frame so the first line reflects a full window.
void MeshLink::reportMeshMix(uint32_t nowMs) {
  if (meshMix_.windowStartMs == 0) meshMix_.windowStartMs = nowMs;
  if (nowMs - meshMix_.windowStartMs < kMeshMixWindowMs) return;
  Serial.printf("[meshmix] win=30s rx: hello=%u wisp_hello=%u paint=%u "
                "control=%u event=%u claim=%u status=%u other=%u | "
                "relayed_out=%u | total_rx=%u\n",
                (unsigned)meshMix_.rx[MeshMix::kHello],
                (unsigned)meshMix_.rx[MeshMix::kWispHello],
                (unsigned)meshMix_.rx[MeshMix::kPaint],
                (unsigned)meshMix_.rx[MeshMix::kControl],
                (unsigned)meshMix_.rx[MeshMix::kEvent],
                (unsigned)meshMix_.rx[MeshMix::kClaim],
                (unsigned)meshMix_.rx[MeshMix::kStatus],
                (unsigned)meshMix_.rx[MeshMix::kOther],
                (unsigned)meshMix_.relayedOut,
                (unsigned)meshMix_.totalRx());
  const uint32_t supp = helloSuppressor_.suppressedWindow();
  const uint32_t rel = helloSuppressor_.relayedWindow();
  const uint32_t fired = supp + rel;
  Serial.printf("[hellosupp] win=30s suppressed=%u relayed=%u rate=%u%%\n",
                (unsigned)supp, (unsigned)rel,
                (unsigned)(fired ? supp * 100 / fired : 0));
  Serial.printf("[wispstate] win=30s recv=%u adopts=%u releases=%u selfPresent=%u\n",
                (unsigned)wispStateMeter_.recv(),
                (unsigned)wispStateMeter_.adopts(),
                (unsigned)wispStateMeter_.releases(),
                (unsigned)wispStateMeter_.selfPresent());
  wispStateMeter_.resetWindow();
  helloSuppressor_.resetWindow();
  meshMix_.reset(nowMs);
}
#endif

void MeshLink::handleRecv(const uint8_t* srcMac, const uint8_t* data,
                              size_t len, int8_t rssi) {
  const uint8_t msgType = lamp_protocol::inspect(data, len);
#ifdef LAMP_DEBUG
  meshMix_.countRx(msgType);
  reportMeshMix(millis());
#endif
  if (msgType == lamp_protocol::MSG_HELLO) {
    lamp_protocol::ParsedHello h;
    if (!lamp_protocol::parseHello(data, len, h)) return;
    // A duplicate still counts toward relay suppression (a neighbor already
    // rebroadcast it) even though it skips the roster reprocess below.
    if (!helloDedup_.record(h.sourceMac, lamp_protocol::MSG_HELLO, h.seq)) {
      helloSuppressor_.onDuplicate(h.sourceMac, h.seq);
      return;
    }
    // Don't rebroadcast own hellos.
    if (std::memcmp(h.sourceMac, myMac_, 6) == 0) return;
#ifdef LAMP_DEBUG
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    {
      static uint32_t lastHelloRxLogMs = 0;
      static uint8_t  lastHelloRxMac[6] = {0};
      uint32_t nowMsLog = millis();
      const bool macChanged = std::memcmp(h.sourceMac, lastHelloRxMac, 6) != 0;
      if (macChanged || (nowMsLog - lastHelloRxLogMs) > 10000) {
        lastHelloRxLogMs = nowMsLog;
        std::memcpy(lastHelloRxMac, h.sourceMac, 6);
        Serial.printf("[show] HELLO recv from %02X:%02X:%02X:%02X:%02X:%02X "
                      "v=0x%08X rssi=%d\n",
                      h.sourceMac[0], h.sourceMac[1], h.sourceMac[2],
                      h.sourceMac[3], h.sourceMac[4], h.sourceMac[5],
                      (unsigned)h.firmwareVersion, (int)rssi);
      }
    }
#endif
#endif
    // RSSI feeds the cascade sort so physically closer peers fire first.
    const std::string peerName = h.nameLen ? std::string(h.name, h.nameLen) : std::string();
    lampRoster.addOrUpdateFromEspNow(
        peerName,
        h.sourceMac,
        Color(h.base[0],  h.base[1],  h.base[2],  h.base[3]),
        Color(h.shade[0], h.shade[1], h.shade[2], h.shade[3]),
        h.firmwareVersion,
        h.otaState,
        // data[2] = protocol version, validated by inspect() before reaching here.
        data[2],
        h.fwChannel,
        h.fsDigest,
        h.hasFsDigest,
        h.maxChunk,
        h.needsFs,
        h.hasOtaSendingTo ? h.otaSendingTo : nullptr,
        h.hasOtaSendingTo,
        rssi);
    if (isDirectHello(srcMac, h.sourceMac) && isNearRssi(rssi, kNearRssiEspNow)) {
      lampRoster.markNear(h.sourceMac);
    }
    if (helloSuppressor_.onFirstSeen(h.sourceMac, h.seq, data, len, millis())) {
      relay(data, len);
    }
  } else if (msgType == lamp_protocol::MSG_CONTROL_OP) {
    lamp_protocol::ParsedControlOp op;
    if (!lamp_protocol::parseControlOp(data, len, op)) return;
    // Dedup by (sourceMac, seq) so a loop-relayed copy doesn't fire twice.
    if (!controlOpDedup_.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP, op.seq)) return;
    // Rebroadcast for grid relay: extends mesh reach beyond direct radio
    // range. CONTROL_OP is unconditionally gossip-relayed.
    relay(data, len);
    // Apply locally when the target is this lamp or broadcast.
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const bool forUs = (std::memcmp(op.targetMac, myMac_, 6) == 0) ||
                       (std::memcmp(op.targetMac, bcast, 6) == 0);
    if (forUs && controlOpHandler_) {
#ifdef LAMP_DEBUG
      Serial.printf("[show] CONTROL_OP apply len=%u src=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    (unsigned)op.payloadLen,
                    op.sourceMac[0], op.sourceMac[1], op.sourceMac[2],
                    op.sourceMac[3], op.sourceMac[4], op.sourceMac[5]);
#endif
      controlOpHandler_(op.payload, op.payloadLen, op.sourceMac);
    }
  } else if (msgType == lamp_protocol::MSG_WISP_HELLO) {
    // Parse and dedup on the WiFi task; cacheWispHello runs on Core 1 via
    // the pending slot so recv-task work stays bounded to memcpy + dedup.
    lamp_protocol::ParsedWispHello h;
    if (!lamp_protocol::parseWispHello(data, len, h)) return;
#ifdef LAMP_DEBUG
    reportWispCoex(h.sourceMac, millis());
    static bool s_loggedWispMacs = false;
    if (!s_loggedWispMacs) {
      s_loggedWispMacs = true;
      Serial.printf("[wispdirect] srcMac=%02X:%02X:%02X:%02X:%02X:%02X "
                    "sourceMac=%02X:%02X:%02X:%02X:%02X:%02X direct=%d\n",
                    srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5],
                    h.sourceMac[0], h.sourceMac[1], h.sourceMac[2],
                    h.sourceMac[3], h.sourceMac[4], h.sourceMac[5],
                    (int)isDirectHello(srcMac, h.sourceMac));
    }
#endif
    if (!wispHelloDedup_.record(h.sourceMac, lamp_protocol::MSG_WISP_HELLO, h.seq)) return;
    // Single-hop relay: only a frame heard straight from the wisp propagates,
    // so lamps behind a coex-dropped broadcast still converge on presence
    // without a fleet-wide flood. A relayed copy (srcMac != sourceMac) stops.
    if (isDirectHello(srcMac, h.sourceMac)) relay(data, len);
    PendingWispHello slot;
    std::memcpy(slot.sourceMac, h.sourceMac, 6);
    slot.wispVersion = h.wispVersion;
    slot.flags = h.flags;
    std::memcpy(slot.paletteIdPrefix, h.paletteIdPrefix,
                lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN);
    std::memcpy(slot.carriedFwChannel, h.carriedFwChannel,
                lamp_protocol::WISP_HELLO_FW_CHANNEL_LEN);
    slot.carriedFwVersion = h.carriedFwVersion;
    postPendingWispHello(slot);
  } else if (msgType == lamp_protocol::MSG_WISP_CLAIM) {
    // No relay: CLAIM relay was the dominant broadcast-load source.
    // Dedup still runs to prevent repeat-fire on direct reception.
    lamp_protocol::ParsedWispClaim wc;
    if (!lamp_protocol::parseWispClaim(data, len, wc)) return;
    if (!wispClaimDedup_.record(wc.sourceMac,
                                lamp_protocol::MSG_WISP_CLAIM, wc.seq)) {
      return;
    }
    // static: the 600 B entry array is too big for the recv-task stack; handleRecv
    // is the only writer and runs single-threaded on the WiFi recv callback.
    static PendingWispClaim slot;
    std::memcpy(slot.sourceMac, wc.sourceMac, 6);
    const uint8_t safeCount = wc.count > lamp_protocol::kMaxWispClaimEntries
                                  ? static_cast<uint8_t>(lamp_protocol::kMaxWispClaimEntries)
                                  : wc.count;
    slot.count = safeCount;
    for (uint8_t i = 0; i < safeCount; ++i) {
      // Each entry is 7 bytes: lampMac[6] + int8 rssi. Copy only the MAC.
      std::memcpy(slot.lampMacs[i],
                  wc.entries + static_cast<size_t>(i) * lamp_protocol::WISP_CLAIM_ENTRY_SIZE,
                  6);
    }
    postPendingWispClaim(slot);
  } else if (msgType == lamp_protocol::MSG_WISP_PALETTE) {
    // Cache feeds CHAR_WISP_STATUS so the app sees the palette through the
    // connected lamp. Single-hop scope: adopt only a directly-heard wisp.
    lamp_protocol::ParsedWispPalette wp;
    if (!lamp_protocol::parseWispPalette(data, len, wp)) return;
    if (!wispPaletteDedup_.record(wp.sourceMac,
                                  lamp_protocol::MSG_WISP_PALETTE, wp.seq)) {
      return;
    }
    // Single-hop relay: only a frame heard straight from the wisp propagates
    // one hop; a relayed copy (srcMac != sourceMac) stops.
    if (isDirectHello(srcMac, wp.sourceMac)) relay(data, len);
    PendingWispPalette slot;
    std::memcpy(slot.sourceMac, wp.sourceMac, 6);
    slot.count = wp.count;
    for (uint8_t i = 0; i < wp.count && wp.rgb; ++i) {
      slot.rgbw[i * 4 + 0] = wp.rgb[i * 3 + 0];
      slot.rgbw[i * 4 + 1] = wp.rgb[i * 3 + 1];
      slot.rgbw[i * 4 + 2] = wp.rgb[i * 3 + 2];
      slot.rgbw[i * 4 + 3] = wp.w ? wp.w[i] : 0;
    }
    postPendingWispPalette(slot);
  } else if (msgType == lamp_protocol::MSG_WISP_PAINT) {
    // No relay: the full-set PAINT frame re-covers a missed frame on its
    // next cadence; a relay would multiply broadcast load fleet-wide. Dedup
    // still runs to prevent repeat-fire on direct reception.
    lamp_protocol::ParsedWispPaint wp;
    if (!lamp_protocol::parseWispPaint(data, len, wp)) return;
    if (!wispPaintDedup_.record(wp.sourceMac,
                                lamp_protocol::MSG_WISP_PAINT, wp.seq)) {
      return;
    }
    // static: the 1200 B entry array is too big for the recv-task stack; handleRecv
    // is the only writer and runs single-threaded on the WiFi recv callback.
    static PendingWispPaint slot;
    std::memcpy(slot.sourceMac, wp.sourceMac, 6);
    slot.count = wp.count;
    if (wp.count > 0 && wp.entries) {
      std::memcpy(slot.entries, wp.entries,
                  static_cast<size_t>(wp.count) *
                      lamp_protocol::WISP_PAINT_ENTRY_SIZE);
    }
    postPendingWispPaint(slot);
  } else if (msgType == lamp_protocol::MSG_WISP_STATE) {
    // No relay: the full-set STATE frame re-covers a missed frame on its next
    // cadence. Dedup prevents repeat-fire on direct reception.
    lamp_protocol::ParsedWispState ws;
    if (!lamp_protocol::parseWispState(data, len, ws)) return;
#ifdef LAMP_DEBUG
    reportWispState(ws.sourceMac, ws, millis());
#endif
    if (!wispStateDedup_.record(ws.sourceMac,
                                lamp_protocol::MSG_WISP_STATE, ws.seq)) {
      return;
    }
    // static: the 1440 B entry array is too big for the recv-task stack; handleRecv
    // is the only writer and runs single-threaded on the WiFi recv callback.
    static PendingWispState slot;
    std::memcpy(slot.sourceMac, ws.sourceMac, 6);
    slot.count = ws.count;
    if (ws.count > 0 && ws.entries) {
      std::memcpy(slot.entries, ws.entries,
                  static_cast<size_t>(ws.count) *
                      lamp_protocol::WISP_STATE_ENTRY_SIZE);
    }
    postPendingWispState(slot);
  } else if (msgType == lamp_protocol::MSG_OVERRIDE_COLORS) {
    lamp_protocol::ParsedOverrideColors p;
    if (!lamp_protocol::parseOverrideColors(data, len, p)) return;
    if (!overrideColorsDedup_.record(p.sourceMac,
                                     lamp_protocol::MSG_OVERRIDE_COLORS,
                                     p.seq)) return;
    // No relay: unicast paint scoped to direct radio range (no spillover).
    if (!addressedToUs(p.targetMac, myMac_)) return;
#ifdef LAMP_DEBUG
    Serial.printf("[recv] OVERRIDE_COLORS surface=0x%02X src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u fade=%ums kind=%u\n",
                  (unsigned)p.surface,
                  p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                  p.sourceMac[3], p.sourceMac[4], p.sourceMac[5],
                  (unsigned)p.seq, (unsigned)p.fadeDurationMs,
                  (unsigned)p.sourceKind);
#endif
    PendingOverrideColors slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.surface = p.surface;
    slot.sourceKind = p.sourceKind;
    slot.fadeDurationMs = p.fadeDurationMs;
    slot.numColors = p.numColors;
    for (uint8_t i = 0; i < p.numColors; ++i) {
      slot.colors[i] = Color(p.colors[i][0], p.colors[i][1],
                             p.colors[i][2], p.colors[i][3]);
    }
    postPendingOverrideColors(slot);
  } else if (msgType == lamp_protocol::MSG_RESTORE_COLORS) {
    lamp_protocol::ParsedRestoreColors p;
    if (!lamp_protocol::parseRestoreColors(data, len, p)) return;
    if (!restoreColorsDedup_.record(p.sourceMac,
                                    lamp_protocol::MSG_RESTORE_COLORS,
                                    p.seq)) return;
    // No relay.
    if (!addressedToUs(p.targetMac, myMac_)) return;
#ifdef LAMP_DEBUG
    Serial.printf("[recv] RESTORE_COLORS surface=0x%02X src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u kind=%u\n",
                  (unsigned)p.surface,
                  p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                  p.sourceMac[3], p.sourceMac[4], p.sourceMac[5],
                  (unsigned)p.seq, (unsigned)p.sourceKind);
#endif
    PendingRestoreColors slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.surface = p.surface;
    slot.sourceKind = p.sourceKind;
    slot.fadeDurationMs = p.fadeDurationMs;
    postPendingRestoreColors(slot);
  } else if (msgType == lamp_protocol::MSG_OVERRIDE_BRIGHTNESS) {
    lamp_protocol::ParsedOverrideBrightness p;
    if (!lamp_protocol::parseOverrideBrightness(data, len, p)) return;
    if (!overrideBrightnessDedup_.record(p.sourceMac,
                                         lamp_protocol::MSG_OVERRIDE_BRIGHTNESS,
                                         p.seq)) return;
    // No relay.
    if (!addressedToUs(p.targetMac, myMac_)) return;
    // Only the wisp claiming this lamp may dim it. Claims arrive in every
    // source mode incl Off, unlike the painter record color paint sets, so
    // this gate holds even for an Off-mode space dim.
    if (!lampRoster.isClaimingWisp(p.sourceMac, millis())) return;
    PendingOverrideBrightness slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.surface = p.surface;
    slot.sourceKind = p.sourceKind;
    slot.fadeDurationMs = p.fadeDurationMs;
    slot.brightness = p.brightness;
    postPendingOverrideBrightness(slot);
  } else if (msgType == lamp_protocol::MSG_RESTORE_BRIGHTNESS) {
    lamp_protocol::ParsedRestoreBrightness p;
    if (!lamp_protocol::parseRestoreBrightness(data, len, p)) return;
    if (!restoreBrightnessDedup_.record(p.sourceMac,
                                        lamp_protocol::MSG_RESTORE_BRIGHTNESS,
                                        p.seq)) return;
    // No relay.
    if (!addressedToUs(p.targetMac, myMac_)) return;
    PendingRestoreBrightness slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.surface = p.surface;
    slot.sourceKind = p.sourceKind;
    slot.fadeDurationMs = p.fadeDurationMs;
    postPendingRestoreBrightness(slot);
  } else if (msgType == lamp_protocol::MSG_COMMAND) {
    lamp_protocol::ParsedCommand p;
    if (!lamp_protocol::parseCommand(data, len, p)) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] COMMAND drop=parse len=%u\n", (unsigned)len);
#endif
      return;
    }
    if (!lamp_protocol::command_auth::verify(data, len - lamp_protocol::COMMAND_TAG_SIZE, p.tag)) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] COMMAND drop=auth src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u\n",
                    p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                    p.sourceMac[3], p.sourceMac[4], p.sourceMac[5], (unsigned)p.seq);
#endif
      return;
    }
    if (!commandDedup_.record(p.sourceMac, lamp_protocol::MSG_COMMAND, p.seq)) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] COMMAND drop=dedup src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u\n",
                    p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                    p.sourceMac[3], p.sourceMac[4], p.sourceMac[5], (unsigned)p.seq);
#endif
      return;
    }
    // No relay.
    if (!addressedToUs(p.targetMac, myMac_)) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] COMMAND drop=addressed src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u\n",
                    p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                    p.sourceMac[3], p.sourceMac[4], p.sourceMac[5], (unsigned)p.seq);
#endif
      return;
    }
#ifdef LAMP_DEBUG
    Serial.printf("[recv] COMMAND accepted src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u\n",
                  p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                  p.sourceMac[3], p.sourceMac[4], p.sourceMac[5], (unsigned)p.seq);
#endif
    // static: the 1444 B payload is too big for the recv-task stack; handleRecv
    // is the only writer and runs single-threaded on the WiFi recv callback.
    static PendingCommand slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.payloadLen = static_cast<uint16_t>(p.payloadLen);
    std::memcpy(slot.payload, p.payload, p.payloadLen);
    postPendingCommand(slot);
  } else if (msgType == lamp_protocol::MSG_EVENT) {
    lamp_protocol::ParsedEvent p;
    if (!lamp_protocol::parseEvent(data, len, p)) return;
    if (!lamp_protocol::command_auth::verify(data, len - lamp_protocol::EVENT_TAG_SIZE, p.tag)) return;
    if (!eventDedup_.record(p.sourceMac, lamp_protocol::MSG_EVENT, p.seq)) return;
    // No relay.
    PendingEvent slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.payloadLen = static_cast<uint16_t>(p.payloadLen);
    std::memcpy(slot.payload, p.payload, p.payloadLen);
    postPendingEvent(slot);
  } else if (msgType == lamp_protocol::MSG_BID) {
    lamp_protocol::ParsedBid p;
    if (!lamp_protocol::parseBid(data, len, p)) return;
    if (!lamp_protocol::command_auth::verify(data, len - lamp_protocol::BID_TAG_SIZE, p.tag)) return;
    if (!bidDedup_.record(p.sourceMac, lamp_protocol::MSG_BID, p.seq)) return;
    // No relay; the Core 1 drain resolves disposition + roster and decides.
    PendingBid slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.bidType = p.bidType;
    postPendingBid(slot);
  } else if (msgType == lamp_protocol::MSG_COLOR_QUERY) {
    lamp_protocol::ParsedColorQuery p;
    if (!lamp_protocol::parseColorQuery(data, len, p)) return;
    if (!colorQueryDedup_.record(p.sourceMac, lamp_protocol::MSG_COLOR_QUERY, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    PendingColorQuery slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    postPendingColorQuery(slot);
  } else if (msgType == lamp_protocol::MSG_COLOR_INFO) {
    lamp_protocol::ParsedColorInfo p;
    if (!lamp_protocol::parseColorInfo(data, len, p)) return;
    if (!colorInfoDedup_.record(p.sourceMac, lamp_protocol::MSG_COLOR_INFO, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    PendingColorInfo slot;
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    slot.baseCount = p.baseCount;
    if (p.baseCount)  std::memcpy(slot.baseStops,  p.baseStops,  p.baseCount * 4);
    slot.shadeCount = p.shadeCount;
    if (p.shadeCount) std::memcpy(slot.shadeStops, p.shadeStops, p.shadeCount * 4);
    postPendingColorInfo(slot);
  } else if (msgType == lamp_protocol::MSG_FW_OFFER) {
    // Single-hop unicast; no gossip relay. The shared firmwareDedup_
    // ring guards against the wisp re-sending the OFFER mid-drain.
    lamp_protocol::ParsedFwOffer p;
    if (!lamp_protocol::parseFwOffer(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_OFFER, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    // Receiver-side signal-floor gate, mirrors the sender's: a weak direct
    // hop drops the OFFER instead of streaming a doomed transfer. Unknown
    // RSSI isn't gated.
    if (rssi != -127 && rssi < lamp_protocol::kOtaMinRssiDbm) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] FW_OFFER decline: rssi=%d below floor %d\n",
                    (int)rssi, (int)lamp_protocol::kOtaMinRssiDbm);
#endif
      return;
    }
    PendingFirmwareControl slot{};
    slot.msgType = lamp_protocol::MSG_FW_OFFER;
    slot.seq = p.seq;
    slot.wireVersion = data[2];
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    std::memcpy(slot.targetMac, p.targetMac, 6);
    slot.offer.version       = p.version;
    slot.offer.totalLen      = p.totalLen;
    slot.offer.chunkSize     = p.chunkSize;
    std::memcpy(slot.offer.channel, p.channel, lamp_protocol::FW_CHANNEL_LEN);
    std::memcpy(slot.offer.sha256Prefix, p.sha256Prefix,
                lamp_protocol::FW_SHA256_PREFIX_LEN);
    slot.offer.footerLen   = p.footerLen;
    slot.offer.totalChunks = p.totalChunks;
    slot.offer.hasAuth     = p.hasAuth;
    std::memcpy(slot.offer.digest, p.digest, lamp_protocol::FW_SHA256_FULL_LEN);
    std::memcpy(slot.offer.signature, p.signature, lamp_protocol::FW_SIG_LEN);
    postPendingFirmwareControl(slot);
  } else if (msgType == lamp_protocol::MSG_FW_CHUNK) {
    // Direct handoff to handleChunkOnRecvTask: a pending single-slot
    // mailbox can't keep up with 250 chunks/s. The handler is bounded
    // (~0.5 ms esp_ota_write_with_offset, portMUX only, no heap/blocking).
    lamp_protocol::ParsedFwChunk p;
    if (!lamp_protocol::parseFwChunk(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_CHUNK, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    if (firmwareReceiver_) firmwareReceiver_->handleChunkOnRecvTask(p);
  } else if (msgType == lamp_protocol::MSG_FW_DONE) {
    lamp_protocol::ParsedFwDone p;
    if (!lamp_protocol::parseFwDone(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_DONE, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    PendingFirmwareControl slot{};
    slot.msgType = lamp_protocol::MSG_FW_DONE;
    slot.seq = p.seq;
    slot.wireVersion = data[2];
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    std::memcpy(slot.targetMac, p.targetMac, 6);
    slot.done.version  = p.version;
    slot.done.totalLen = p.totalLen;
    std::memcpy(slot.done.sha256Prefix, p.sha256Prefix,
                lamp_protocol::FW_SHA256_PREFIX_LEN);
    slot.done.footerLen = p.footerLen;
    postPendingFirmwareControl(slot);
  } else if (msgType == lamp_protocol::MSG_FW_ACCEPT) {
    // Route distributor-protocol frames (ACCEPT/REQ/RESULT) to FirmwareDistributor
    // on this recv task; its state machine handles them under portMUX.
    lamp_protocol::ParsedFwAccept p;
    if (!lamp_protocol::parseFwAccept(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_ACCEPT, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    if (firmwareDistributor_) firmwareDistributor_->onAcceptOnRecvTask(p);
  } else if (msgType == lamp_protocol::MSG_FW_REQ) {
    lamp_protocol::ParsedFwReq p;
    if (!lamp_protocol::parseFwReq(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_REQ, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    if (firmwareDistributor_) firmwareDistributor_->onReqOnRecvTask(p);
  } else if (msgType == lamp_protocol::MSG_FW_RESULT) {
    lamp_protocol::ParsedFwResult p;
    if (!lamp_protocol::parseFwResult(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_RESULT, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    if (firmwareDistributor_) firmwareDistributor_->onResultOnRecvTask(p);
  } else if (msgType == lamp_protocol::MSG_FS_OFFER) {
    lamp_protocol::ParsedFwOffer p;
    if (!lamp_protocol::parseFwOffer(data, len, p, lamp_protocol::MSG_FS_OFFER)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_OFFER, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    // Receiver-side signal-floor gate, mirrors the sender's: a weak direct
    // hop drops the OFFER instead of streaming a doomed transfer. Unknown
    // RSSI isn't gated.
    if (rssi != -127 && rssi < lamp_protocol::kOtaMinRssiDbm) {
#ifdef LAMP_DEBUG
      Serial.printf("[recv] FS_OFFER decline: rssi=%d below floor %d\n",
                    (int)rssi, (int)lamp_protocol::kOtaMinRssiDbm);
#endif
      return;
    }
    PendingFirmwareControl slot{};
    slot.msgType = lamp_protocol::MSG_FS_OFFER;
    slot.seq = p.seq;
    slot.wireVersion = data[2];
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    std::memcpy(slot.targetMac, p.targetMac, 6);
    slot.offer.version   = p.version;
    slot.offer.totalLen  = p.totalLen;
    slot.offer.chunkSize = p.chunkSize;
    std::memcpy(slot.offer.channel, p.channel, lamp_protocol::FW_CHANNEL_LEN);
    std::memcpy(slot.offer.sha256Prefix, p.sha256Prefix,
                lamp_protocol::FW_SHA256_PREFIX_LEN);
    slot.offer.footerLen   = p.footerLen;
    slot.offer.totalChunks = p.totalChunks;
    slot.offer.hasAuth     = p.hasAuth;
    std::memcpy(slot.offer.digest, p.digest, lamp_protocol::FW_SHA256_FULL_LEN);
    std::memcpy(slot.offer.signature, p.signature, lamp_protocol::FW_SIG_LEN);
    postPendingFirmwareControl(slot);
  } else if (msgType == lamp_protocol::MSG_FS_CHUNK) {
    lamp_protocol::ParsedFwChunk p;
    if (!lamp_protocol::parseFwChunk(data, len, p, lamp_protocol::MSG_FS_CHUNK)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_CHUNK, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    fs_ota::onChunk(p);
  } else if (msgType == lamp_protocol::MSG_FS_DONE) {
    lamp_protocol::ParsedFwDone p;
    if (!lamp_protocol::parseFwDone(data, len, p, lamp_protocol::MSG_FS_DONE)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_DONE, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    PendingFirmwareControl slot{};
    slot.msgType = lamp_protocol::MSG_FS_DONE;
    slot.seq = p.seq;
    slot.wireVersion = data[2];
    std::memcpy(slot.sourceMac, p.sourceMac, 6);
    std::memcpy(slot.targetMac, p.targetMac, 6);
    slot.done.version   = p.version;
    slot.done.totalLen  = p.totalLen;
    std::memcpy(slot.done.sha256Prefix, p.sha256Prefix,
                lamp_protocol::FW_SHA256_PREFIX_LEN);
    slot.done.footerLen = p.footerLen;
    postPendingFirmwareControl(slot);
  } else if (msgType == lamp_protocol::MSG_FS_ACCEPT) {
    lamp_protocol::ParsedFwAccept p;
    if (!lamp_protocol::parseFwAccept(data, len, p, lamp_protocol::MSG_FS_ACCEPT)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_ACCEPT, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    fs_ota::onAccept(p);
  } else if (msgType == lamp_protocol::MSG_FS_REQ) {
    lamp_protocol::ParsedFwReq p;
    if (!lamp_protocol::parseFwReq(data, len, p, lamp_protocol::MSG_FS_REQ)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_REQ, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    fs_ota::onReq(p);
  } else if (msgType == lamp_protocol::MSG_FS_RESULT) {
    lamp_protocol::ParsedFwResult p;
    if (!lamp_protocol::parseFwResult(data, len, p, lamp_protocol::MSG_FS_RESULT)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FS_RESULT, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
    fs_ota::onResult(p);
  }
}

void MeshLink::emitHello(uint8_t otaState) {
  if (!config_) return;
#ifdef LAMP_DEBUG
  static uint32_t lastEmitLogMs = 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  uint32_t nowMsLog = millis();
  if (nowMsLog - lastEmitLogMs > 10000) {
    lastEmitLogMs = nowMsLog;
    Serial.printf("[show] emitHello v=0x%08X seq=%u\n",
                  (unsigned)FIRMWARE_VERSION, (unsigned)helloSeq_);
  }
#endif
#endif
  uint8_t shade[4] = {0, 0, 0, 0};
  uint8_t base[4]  = {0, 0, 0, 0};
  if (!config_->shade.broadcastColors().empty()) {
    const auto& stops = config_->shade.broadcastColors();
    const Color c = lampos::led::blendedIdentity(stops.data(), stops.size());
    shade[0] = c.r; shade[1] = c.g; shade[2] = c.b; shade[3] = c.w;
  }
  if (!config_->base.broadcastColors().empty()) {
    const auto& stops = config_->base.broadcastColors();
    const Color c = lampos::led::blendedIdentity(stops.data(), stops.size());
    base[0] = c.r; base[1] = c.g; base[2] = c.b; base[3] = c.w;
  }

  const std::string& name = config_->lamp.name;
  const size_t nameLen = name.size() > lamp_protocol::HELLO_MAX_NAME
                           ? lamp_protocol::HELLO_MAX_NAME
                           : name.size();

  // buildHello omits the TLV entirely when kOtaStateIdle.
  uint8_t buf[lamp_protocol::HELLO_MAX_SIZE];
  // OTA_SENDING_TO names the single peer being distributed to (valid only
  // mid-distribution) so the app can mark the HELLO-silent receiver.
  uint8_t sendingTo[6];
  const bool hasSendingTo =
      otaState == lamp_protocol::kOtaStateSending &&
      firmwareDistributor_ && firmwareDistributor_->getPeerMac(sendingTo);
  // FW_MAX_CHUNK advertises this lamp's OTA-receive ceiling so a peer's
  // distributor can negotiate a larger session chunk size than the baseline.
  // NEED_FS (re-evaluated every HELLO) asks peers to offer the UI image while
  // this lamp has no valid FS digest to advertise a mismatch against.
  size_t n = lamp_protocol::buildHello(buf, sizeof(buf), helloSeq_++, myMac_,
                                       shade, base, FIRMWARE_VERSION,
                                       name.data(), nameLen, otaState,
                                       FIRMWARE_CHANNEL_STR,
                                       fs_ota::localDigestPrefix(),
                                       lamp_protocol::FW_CHUNK_SIZE_MAX,
                                       fs_ota::needsFs(),
                                       hasSendingTo ? sendingTo : nullptr);
  if (n) {
    link_.broadcast(buf, n);
  }
}

}  // namespace lamp
