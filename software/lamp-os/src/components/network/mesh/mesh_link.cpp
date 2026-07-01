#include "mesh_link.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/fs_ota.hpp"
#include "version.hpp"

namespace lamp {

MeshLink* MeshLink::s_instance = nullptr;

void MeshLink::onRecv(const uint8_t* mac, const uint8_t* data, size_t len,
                          int8_t rssi) {
  if (s_instance) s_instance->handleRecv(mac, data, len, rssi);
}

void MeshLink::begin(Config* cfg) {
  config_ = cfg;
  s_instance = this;
  if (!link_.begin(&MeshLink::onRecv)) {
    Serial.println("[show] ESP-NOW init failed");
    return;
  }
  link_.getMac(myMac_);
  Serial.printf("[show] ready, mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                myMac_[0], myMac_[1], myMac_[2], myMac_[3], myMac_[4], myMac_[5]);
}

void MeshLink::tick() {
  uint32_t now = millis();
  if (now - lastHelloMs_ < LAMP_HELLO_INTERVAL_MS && lastHelloMs_ != 0) return;
  lastHelloMs_ = now;
  // Suppress HELLO during gossip OTA so the chunk stream gets channel time.
  if (isOtaInProgress()) return;
  emitHello();
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
  return link_.broadcast(buf, n);
}

bool MeshLink::broadcastRaw(const uint8_t* data, size_t len) {
  return link_.broadcast(data, len);
}

uint16_t MeshLink::nextEventSeq() {
  return eventSeq_++;
}

// Broadcast is all-FF.
static bool addressedToUs(const uint8_t targetMac[6], const uint8_t myMac[6]) {
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return std::memcmp(targetMac, myMac, 6) == 0 ||
         std::memcmp(targetMac, bcast, 6) == 0;
}

void MeshLink::handleRecv(const uint8_t* /*srcMac*/, const uint8_t* data,
                              size_t len, int8_t rssi) {
  const uint8_t msgType = lamp_protocol::inspect(data, len);
  if (msgType == lamp_protocol::MSG_HELLO) {
    lamp_protocol::ParsedHello h;
    if (!lamp_protocol::parseHello(data, len, h)) return;
    if (!helloDedup_.record(h.sourceMac, lamp_protocol::MSG_HELLO, h.seq)) return;
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
    nearbyLamps.addOrUpdateFromEspNow(
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
        h.hasFsDigest);
    link_.broadcast(data, len);
  } else if (msgType == lamp_protocol::MSG_CONTROL_OP) {
    lamp_protocol::ParsedControlOp op;
    if (!lamp_protocol::parseControlOp(data, len, op)) return;
    // Dedup by (sourceMac, seq) so a loop-relayed copy doesn't fire twice.
    if (!controlOpDedup_.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP, op.seq)) return;
    // Rebroadcast for grid relay: extends mesh reach beyond direct radio
    // range. CONTROL_OP is unconditionally gossip-relayed.
    link_.broadcast(data, len);
    // Apply locally if addressed to us or broadcast.
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
    if (!wispHelloDedup_.record(h.sourceMac, lamp_protocol::MSG_WISP_HELLO, h.seq)) return;
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
    // Gossip-relay so wisps beyond direct range propagate.
    link_.broadcast(data, len);
  } else if (msgType == lamp_protocol::MSG_WISP_CLAIM) {
    // No relay: CLAIM relay was the dominant broadcast-load source.
    // Dedup still runs to prevent repeat-fire on direct reception.
    lamp_protocol::ParsedWispClaim wc;
    if (!lamp_protocol::parseWispClaim(data, len, wc)) return;
    if (!wispClaimDedup_.record(wc.sourceMac,
                                lamp_protocol::MSG_WISP_CLAIM, wc.seq)) {
      return;
    }
    PendingWispClaim slot;
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
    // Cache and gossip-relay; cache feeds CHAR_WISP_STATUS so the app sees
    // the palette through any connected lamp. Dedup gates the relay.
    lamp_protocol::ParsedWispPalette wp;
    if (!lamp_protocol::parseWispPalette(data, len, wp)) return;
    if (!wispPaletteDedup_.record(wp.sourceMac,
                                  lamp_protocol::MSG_WISP_PALETTE, wp.seq)) {
      return;
    }
    PendingWispPalette slot;
    std::memcpy(slot.sourceMac, wp.sourceMac, 6);
    slot.count = wp.count;
    if (wp.count > 0 && wp.rgb) {
      std::memcpy(slot.rgb, wp.rgb,
                  static_cast<size_t>(wp.count) *
                      lamp_protocol::WISP_PALETTE_ENTRY_SIZE);
    }
    postPendingWispPalette(slot);
    link_.broadcast(data, len);
  } else if (msgType == lamp_protocol::MSG_OVERRIDE_COLORS) {
    lamp_protocol::ParsedOverrideColors p;
    if (!lamp_protocol::parseOverrideColors(data, len, p)) return;
    if (!overrideColorsDedup_.record(p.sourceMac,
                                     lamp_protocol::MSG_OVERRIDE_COLORS,
                                     p.seq)) return;
    // No relay: unicast paint scoped to direct radio range (no spillover).
    if (!addressedToUs(p.targetMac, myMac_)) return;
    Serial.printf("[recv] OVERRIDE_COLORS surface=0x%02X src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u fade=%ums kind=%u\n",
                  (unsigned)p.surface,
                  p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                  p.sourceMac[3], p.sourceMac[4], p.sourceMac[5],
                  (unsigned)p.seq, (unsigned)p.fadeDurationMs,
                  (unsigned)p.sourceKind);
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
    Serial.printf("[recv] RESTORE_COLORS surface=0x%02X src=%02X:%02X:%02X:%02X:%02X:%02X seq=%u kind=%u\n",
                  (unsigned)p.surface,
                  p.sourceMac[0], p.sourceMac[1], p.sourceMac[2],
                  p.sourceMac[3], p.sourceMac[4], p.sourceMac[5],
                  (unsigned)p.seq, (unsigned)p.sourceKind);
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
    // Wisp-paired sources bypass the brightness floor (wisp owns go-dark scenes);
    // match sourceMac against the cached hello sender within the pairing window.
    if (p.brightness < lamp_protocol::kBrightnessOverrideMin) {
      const auto wisp = nearbyLamps.getWispCache();
      const uint32_t now = millis();
      // 60 s matches the override watchdog; a wisp silent this long loses the bypass.
      constexpr uint32_t kWispPairingWindowMs = 60000;
      const bool wispPaired = wisp.present &&
                              std::memcmp(wisp.mac, p.sourceMac, 6) == 0 &&
                              (now - wisp.lastHelloMs) < kWispPairingWindowMs;
      if (!wispPaired) return;  // drop silently (defeat-the-defeat)
    }
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
  } else if (msgType == lamp_protocol::MSG_EVENT) {
    lamp_protocol::ParsedEvent ev;
    if (!lamp_protocol::parseEvent(data, len, ev)) return;
    // Dedup by (sourceMac, seq). The cascade sender emits two back-to-back
    // copies of MSG_EVENT for broadcast-loss resilience (ESP-NOW broadcasts
    // are not link-layer ACK'd); both share the same seq so the second
    // copy collapses here. Also collapses any relayed copy.
    if (!eventDedup_.record(ev.sourceMac, lamp_protocol::MSG_EVENT, ev.seq)) return;
    // Drop self-originated broadcast: would otherwise re-trigger locally.
    // The originator already fires on its own clock; no wire round-trip.
    if (std::memcmp(ev.sourceMac, myMac_, 6) == 0) return;
    // Gossip-relay. Dedup ring bounds the storm to N+1 copies in an N-lamp mesh.
    // Relay runs before the eventKind filter for forward-compat with unknown kinds.
    link_.broadcast(data, len);
    // Drop unknown event kinds silently for forward-compat.
    if (ev.eventKind != lamp_protocol::EventKind::ExpressionTriggered) return;
    // Tail-fire at numStaggerEntries * kTailFireStaggerMs when this lamp
    // is absent from the stagger list (truncated or late-join).
    constexpr uint16_t kTailFireStaggerMs = 50;
    uint16_t delayMs = static_cast<uint16_t>(ev.numStaggerEntries) * kTailFireStaggerMs;
    for (uint8_t i = 0; i < ev.numStaggerEntries; ++i) {
      if (std::memcmp(ev.staggerEntries[i].mac, myMac_, 6) == 0) {
        delayMs = ev.staggerEntries[i].delayMs;
        break;
      }
    }
    // Guards against a future payload expansion overflowing PendingEvent.payload.
    if (ev.payloadLen > lamp_protocol::maxEventPayloadFor(ev.numStaggerEntries)) return;
    PendingEvent slot;
    std::memcpy(slot.sourceMac, ev.sourceMac, 6);
    slot.delayMs = delayMs;
    slot.payloadLen = ev.payloadLen;
    if (ev.payloadLen && ev.payload) {
      std::memcpy(slot.payload, ev.payload, ev.payloadLen);
    }
    postPendingEvent(slot);
  } else if (msgType == lamp_protocol::MSG_FW_OFFER) {
    // Single-hop unicast; no gossip relay. The shared firmwareDedup_
    // ring guards against the wisp re-sending the OFFER mid-drain.
    lamp_protocol::ParsedFwOffer p;
    if (!lamp_protocol::parseFwOffer(data, len, p)) return;
    if (!firmwareDedup_.record(p.sourceMac, lamp_protocol::MSG_FW_OFFER, p.seq)) return;
    if (!addressedToUs(p.targetMac, myMac_)) return;
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

void MeshLink::emitHello() {
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
  if (!config_->shade.colors.empty()) {
    const Color& c = config_->shade.colors[0];
    shade[0] = c.r; shade[1] = c.g; shade[2] = c.b; shade[3] = c.w;
  }
  if (!config_->base.colors.empty()) {
    size_t ac = config_->base.ac;
    if (ac >= config_->base.colors.size()) ac = 0;
    const Color& c = config_->base.colors[ac];
    base[0] = c.r; base[1] = c.g; base[2] = c.b; base[3] = c.w;
  }

  const std::string& name = config_->lamp.name;
  const size_t nameLen = name.size() > lamp_protocol::HELLO_MAX_NAME
                           ? lamp_protocol::HELLO_MAX_NAME
                           : name.size();

  // HELLO_TLV_OTA_STATE: receiver status takes priority over distributor.
  // buildHello omits the TLV entirely when kOtaStateIdle.
  uint8_t otaState = lamp_protocol::kOtaStateIdle;
  if (firmwareReceiver_ && firmwareReceiver_->isInProgress()) {
    otaState = lamp_protocol::kOtaStateReceiving;
  } else if (firmwareDistributor_ && firmwareDistributor_->isInProgress()) {
    otaState = lamp_protocol::kOtaStateSending;
  }

  uint8_t buf[lamp_protocol::HELLO_MAX_SIZE];
  size_t n = lamp_protocol::buildHello(buf, sizeof(buf), helloSeq_++, myMac_,
                                       shade, base, FIRMWARE_VERSION,
                                       name.data(), nameLen, otaState,
                                       FIRMWARE_CHANNEL_STR,
                                       fs_ota::localDigestPrefix());
  if (n) {
    link_.broadcast(buf, n);
  }
}

}  // namespace lamp
