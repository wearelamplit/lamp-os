#include "firmware_receiver.hpp"

#include <cstring>

#include "firmware_signature.hpp"
#include "../network/ble_control.hpp"  // pauseRadioForOta / resumeRadioAfterOta
#include "core/ota_quiet_mode.hpp"     // enterQuiet / exitQuiet
#include "../network/mesh_link.hpp"
#include "../../version.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <spi_flash_mmap.h>  // SPI_FLASH_SEC_SIZE
// Low-level IWDT (Interrupt-Watchdog) control. A W25Q block erase can take
// 50-750 ms under cache-disable bursts, longer than the IWDT stage timeout,
// firing rst:0x8 (TG1WDT_SYS_RESET) mid-erase unless widened first.
// custom_sdkconfig raises CONFIG_ESP_INT_WDT_TIMEOUT_MS to 1000, but IDF's
// tick_hook re-applies that compile-time literal every FreeRTOS tick, so a
// runtime erase exceeding the budget can still trip between ticks. These
// hal/wdt_hal.h (not-public-api) calls widen the stage timeout mid-erase.
#include "hal/wdt_hal.h"
#include "hal/mwdt_ll.h"
#include "soc/timer_group_reg.h"
#endif

namespace lamp {

namespace {

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// Restore the default TWDT (5 s, IDLE0 subscribed) on every OTA flow exit.
// Don't subscribe the Arduino loop task: arduino-esp32 leaves
// loopTaskWDTEnabled=false and never calls esp_task_wdt_reset() from the loop
// body, so subscribing it would panic the instant the timeout elapsed.
void restoreDefaultWdt() {
  esp_task_wdt_config_t wdtDefault = {
      .timeout_ms     = 5000,
      .idle_core_mask = (1u << 0),  // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtDefault);
}
#endif

constexpr uint32_t kChunkStallReqMs    = 2000;   // emit REQ after 2s gap
constexpr uint32_t kStreamingHardCapMs = 600000; // total OTA budget (10 min)
// If no chunk arrives for this long the offerer has likely died; abort the
// Streaming state back to Idle and resume HELLOs so any peer can re-OFFER.
// Without it, receivers stay locked to dead offerers until the 5-min hard cap.
constexpr uint32_t kNoProgressAbortMs  = 60000;  // 1 min no-chunk abort
constexpr uint32_t kPostResultPauseMs  = 100;    // pre-restart delay

// ACCEPT burst spread. First ACCEPT goes out synchronously from onOfferOnLoop;
// the rest are scheduled at kAcceptSpreadMs and drained from tick(). The ~1.6 s
// spread overlaps enough BLE adv intervals that one ACCEPT lands in a WiFi-RX
// coex window. Quenched on the first chunk (proof an ACCEPT got through).
constexpr uint8_t  kAcceptBurstCount   = 5;
constexpr uint32_t kAcceptSpreadMs     = 400;

// Full-upfront erase. The whole image region is erased once on Core 1 in
// onOfferOnLoop before the gate arms, so each chunk arrival on Core 0 is a
// pure esp_partition_write. No erase on the recv path.
//
// The erase is synchronous and stalls the main loop for ~1-2 s (longer on a
// slow W25Q). That stall is intentional. It runs before ACCEPT, so no stream
// is starved, and an erase-free recv path keeps cache+IRQ-off stalls from
// overflowing the ESP-NOW RX queue mid-stream. The lamp stops animating for
// that window and resumes normally after. A brief freeze buys the
// reliability of a sensitive flash operation.

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// IWDT widening for the OTA erase window. Prebuilt arduino-esp32 bakes
// CONFIG_ESP_INT_WDT_TIMEOUT_MS=300 and IDF's tick_hook re-applies that ceiling
// every FreeRTOS tick; a 50-750 ms W25Q erase would trip it. Re-widen before
// each erase. The tick_hook can clamp back in the sub-ms window before
// esp_partition_erase_range disables interrupts, but the 1000 ms sdkconfig
// baseline covers a worst-case erase and interrupts go off once the erase
// starts, so the tick_hook can't clamp inside the cache-disabled window.
// mwdt_dev = &TIMERG1 matches int_wdt.c's IWDT_INSTANCE=WDT_MWDT1 (TG1 is the
// IWDT; TG0 holds the task watchdog).
constexpr uint32_t kIwdtEraseStageMs  = 8000;  // stage 0 (interrupt) widen
constexpr uint32_t kIwdtEraseResetMs  = 16000; // stage 1 (system reset) widen
constexpr uint32_t kIwdtTicksPerUs    = 500;   // matches IDF's IWDT_TICKS_PER_US

inline void widenIwdt() {
  wdt_hal_context_t hal = {};
  hal.inst = WDT_MWDT1;
  hal.mwdt_dev = &TIMERG1;
  wdt_hal_write_protect_disable(&hal);
  wdt_hal_config_stage(&hal, WDT_STAGE0,
                       kIwdtEraseStageMs * 1000u / kIwdtTicksPerUs,
                       WDT_STAGE_ACTION_INT);
  wdt_hal_config_stage(&hal, WDT_STAGE1,
                       kIwdtEraseResetMs * 1000u / kIwdtTicksPerUs,
                       WDT_STAGE_ACTION_RESET_SYSTEM);
  wdt_hal_feed(&hal);
  wdt_hal_write_protect_enable(&hal);
}
#endif

// Channel match against this lamp's compiled-in channel. Both sides are
// zero-padded to FW_CHANNEL_LEN before compare so "stable" vs "stable\0\0"
// match correctly.
bool channelMatchesOurs(const char* offerChannel /* FW_CHANNEL_LEN bytes */) {
  char ours[lamp_protocol::FW_CHANNEL_LEN] = {0};
  const char* src = FIRMWARE_CHANNEL_STR ? FIRMWARE_CHANNEL_STR : "";
  size_t srcLen = 0;
  while (src[srcLen] != '\0' && srcLen < lamp_protocol::FW_CHANNEL_LEN) ++srcLen;
  std::memcpy(ours, src, srcLen);
  return std::memcmp(offerChannel, ours, lamp_protocol::FW_CHANNEL_LEN) == 0;
}

}  // namespace

void FirmwareReceiver::begin(FirmwareTransport* meshTransport,
                             FirmwareTransport* bleTransport) {
  meshTransport_ = meshTransport;
  bleTransport_  = bleTransport;
  // Snapshot the lamp's chip MAC from whichever transport is wired (both report
  // the same transport-agnostic identity).
  if (meshTransport_) {
    meshTransport_->getMyMac(myMac_);
  } else if (bleTransport_) {
    bleTransport_->getMyMac(myMac_);
  }
  state_ = State::Idle;
  publishedOtaHandle_.store(0, std::memory_order_relaxed);
}

void FirmwareReceiver::tick(uint32_t nowMs) {
  switch (state_) {
    case State::Idle:
      // Inter-session quiet hold expiry: a prior session deferred its exitQuiet
      // (abortOta / State::Failed) so the strip doesn't flash between
      // back-to-back OFFERs. quietHoldUntilMs_==0 is the sentinel; without the
      // !=0 guard, nowMs>=0 is always true and any Idle tick exits prematurely.
      if (quietHeld_ && quietHoldUntilMs_ != 0 && nowMs >= quietHoldUntilMs_) {
        ::lamp::ota_quiet_mode::exitQuiet();
        quietHeld_        = false;
        quietHoldUntilMs_ = 0;
      }
      return;
    case State::Apply:
      return;

    case State::Streaming: {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      // Drain at most one queued ACCEPT retry per tick (the spread is the
      // point) and quench the queue once a chunk arrives. recvChunksCount_
      // resets to 0 at session start, so non-zero here means the first chunk
      // landed.
      if (pendingAcceptCount_ != 0 && recvChunksCount_ != 0) {
        pendingAcceptCount_ = 0;
      }
      if (pendingAcceptCount_ != 0 &&
          static_cast<int32_t>(nowMs - nextAcceptMs_) >= 0) {
        sendAccept(pendingAcceptCtrl_, lamp_protocol::FwAcceptStatus::Accept);
        --pendingAcceptCount_;
        nextAcceptMs_ = nowMs + kAcceptSpreadMs;
      }

      // No per-chunk erase: the image region was erased upfront in
      // onOfferOnLoop. Lost chunks converge via REQs.
#endif
      // Hard cap on the whole Accepted -> DONE window. On exceed, abort and
      // report PartitionWriteFail with sentinel detail 0xFE so the wisp can
      // distinguish a stalled stream from a real flash write error.
      if (nowMs - streamingStartMs_ > kStreamingHardCapMs) {
#ifdef LAMP_DEBUG
        Serial.println("[fw_receiver] streaming hard cap exceeded, aborting");
#endif
        abortOta();
        sendResult(lamp_protocol::FwResultStatus::PartitionWriteFail, 0xFE);
        state_ = State::Idle;
        return;
      }
      // No-progress abort: if no chunk has arrived for kNoProgressAbortMs the
      // offerer likely died; abort to Idle and resume HELLOs so a peer can
      // re-OFFER (Streaming suppresses HELLO, so otherwise no peer finds this
      // receiver until the 5-min hard cap).
      // Gated on lastChunkSeenMs_ (any arrival) not lastChunkMs_ (successful
      // write): a write-failure streak with chunks still arriving is not a dead
      // session, the stall watchdog will REQ the holes. Signed cast because
      // Core 0 can bump lastChunkSeenMs_ a few ms past our captured nowMs, and
      // unsigned subtraction would wrap to ~4 billion (spurious abort).
      const int32_t elapsedSeen =
          static_cast<int32_t>(nowMs - lastChunkSeenMs_);
      if (lastChunkSeenMs_ != 0 &&
          elapsedSeen > static_cast<int32_t>(kNoProgressAbortMs)) {
#ifdef LAMP_DEBUG
        Serial.printf("[fw_receiver] no chunk arrival for %dms, aborting to Idle\n",
                      (int)elapsedSeen);
#endif
        abortOta();
        state_ = State::Idle;
        return;
      }
      // Stall watchdog: if 2s have elapsed since the last chunk, emit one
      // MSG_FW_REQ for the lowest unset chunk index. Rate-limited so a
      // back-to-back stall doesn't spam REQs.
      if (lastChunkMs_ != 0 && (nowMs - lastChunkMs_) > kChunkStallReqMs &&
          (lastReqMs_ == 0 || (nowMs - lastReqMs_) > kChunkStallReqMs)) {
        const uint16_t firstMissing = firstMissingChunk();
        if (firstMissing != UINT16_MAX) {
          const uint16_t runLen = firstMissingRunLen(firstMissing);
          // runLen >= 1 by definition when firstMissing is valid.
#ifdef LAMP_DEBUG
          Serial.printf("[fw_receiver] stall watchdog: missing chunkIdx=%u run=%u\n",
                        (unsigned)firstMissing, (unsigned)runLen);
#endif
          sendReq(firstMissing, runLen,
                  lamp_protocol::FwReqReason::StallWatchdog);
          lastReqMs_ = nowMs;
        }
      }
      // Every 256 newly-received chunks, log the running total to compare
      // against the wisp's stream-progress log (localises RF loss vs wisp
      // throughput).
      if (recvChunksCount_ - recvChunksLastLog_ >= 256) {
#ifdef LAMP_DEBUG
        Serial.printf("[fw_receiver] recv progress: %u chunks received\n",
                      (unsigned)recvChunksCount_);
#endif
        recvChunksLastLog_ = recvChunksCount_;
      }
      return;
    }

    case State::Failed: {
      // One-tick latch so tests can observe a failure before flipping back to
      // Idle.
      state_ = State::Idle;
      // Most failure paths route through abortOta() which already deferred the
      // exit. The verify-fail branch in handleDoneOnLoop sets State::Failed
      // directly, so defer here too to keep the strip in indicator mode through
      // a fast multi-distributor handoff. The Idle tick expires the hold.
      if (quietHeld_) {
        quietHoldUntilMs_ = nowMs + kInterSessionQuietHoldMs;
      }
      return;
    }

    case State::Verify:
      // Verify resolves synchronously in handleControlOnLoop, not from tick().
      return;
  }
}

void FirmwareReceiver::handleControlOnLoop(const PendingFirmwareControl& ctrl) {
  const uint32_t nowMs =
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      millis();
#else
      0;
#endif
  if (ctrl.msgType == lamp_protocol::MSG_FW_OFFER) {
    onOfferOnLoop(ctrl, nowMs);
  } else if (ctrl.msgType == lamp_protocol::MSG_FW_DONE) {
    onDoneOnLoop(ctrl, nowMs);
  }
}

void FirmwareReceiver::onOfferOnLoop(const PendingFirmwareControl& ctrl,
                                     uint32_t nowMs) {
  // Channel mismatch: silent drop, no ACCEPT or RESULT. Cross-channel offers
  // are a wisp-side target-picker bug and shouldn't add ack-stream noise.
  if (!channelMatchesOurs(ctrl.offer.channel)) {
#ifdef LAMP_DEBUG
    char ch[lamp_protocol::FW_CHANNEL_LEN + 1] = {0};
    std::memcpy(ch, ctrl.offer.channel, lamp_protocol::FW_CHANNEL_LEN);
    Serial.printf("[fw_receiver] OFFER channel=%s ours=%s → silent drop\n",
                  ch, FIRMWARE_CHANNEL_STR);
#endif
    return;
  }
  // Chunk size mismatch: decline-busy (no negotiation, locked to 200).
  // DeclineBusy signals the wisp to stop retrying.
  if (ctrl.offer.chunkSize != lamp_protocol::FW_CHUNK_SIZE) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] OFFER chunkSize=%u != %u, declining\n",
                  (unsigned)ctrl.offer.chunkSize,
                  (unsigned)lamp_protocol::FW_CHUNK_SIZE);
#endif
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineBusy);
    return;
  }
  // Already-current decline. Firmware: version <= ours. FS: the hook declines
  // when the offer isn't the running firmware version OR the offered digest
  // matches the installed image. The peer then drops this lamp from its
  // needs-update set.
  const bool declineCurrent =
      fsHooks_
          ? !fsHooks_->shouldAccept(ctrl.offer.version, ctrl.offer.sha256Prefix)
          : (ctrl.offer.version <= FIRMWARE_VERSION);
  if (declineCurrent) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] OFFER v=0x%08X declined (already current)\n",
                  (unsigned)ctrl.offer.version);
#endif
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineAlreadyCurrent);
    return;
  }
  // Single-source-at-a-time. An idempotent re-OFFER (same version, transport,
  // and source) re-ACKs so a wisp that missed the first ACCEPT can restart.
  // Anything else (different version/transport/source, e.g. a BLE app trying to
  // interrupt a mesh OTA) is DeclineBusy, which stops two concurrent flows
  // writing the same OTA partition.
  if (state_ == State::Streaming || state_ == State::Verify) {
    const bool sameTransport = ctrl.transportKind == activeTransportKind_;
    bool sameSource = false;
    if (sameTransport) {
      if (activeTransportKind_ == FirmwareTransportKind::EspNow) {
        sameSource = (std::memcmp(ctrl.sourceMac, wispMac_, 6) == 0);
      } else {
        sameSource = (ctrl.bleConnHandle == activeBleConnHandle_);
      }
    }
    if (ctrl.offer.version == offerVersion_ && sameTransport && sameSource) {
      sendAccept(ctrl, lamp_protocol::FwAcceptStatus::Accept);
      return;
    }
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineBusy);
    return;
  }

  // Cross-OTA guard: don't start while the other OTA path (FS vs firmware) is
  // mid-write; they share the upfront-erase + quiet-mode machinery. Decline-busy
  // so the peer backs off and retries.
  if (busyGuard_ && busyGuard_()) {
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineBusy);
    return;
  }

  // Begin a new OTA flow. Erase the entire image region synchronously here on
  // Core 1 before arming the gate or sending ACCEPT, so the recv path is a pure
  // write. Order matters: snapshot all OFFER fields into members first (Core 0
  // reads offerTotalLen_/offerChunkSize_ once the gate arms), resize the bitmap,
  // erase, publish the partition pointer, release-store the gate, then ACCEPT.

  // Snapshot OFFER fields before anything Core 0 can observe. offerTotalLen_
  // feeds handleChunkOnRecvTask's bounds check; if it's still zero when a chunk
  // arrives the check passes spuriously and writes land past the declared
  // length.
  std::memcpy(wispMac_, ctrl.sourceMac, 6);
  // Transport context for the active flow; the busy-check on a later OFFER uses
  // these to enforce single-source semantics.
  activeTransportKind_ = ctrl.transportKind;
  activeBleConnHandle_ = ctrl.bleConnHandle;
  activeWireVersion_   = ctrl.wireVersion;
  offerVersion_       = ctrl.offer.version;
  offerTotalLen_      = ctrl.offer.totalLen;
  offerChunkSize_     = ctrl.offer.chunkSize;
  offerTotalChunks_   = ctrl.offer.totalChunks;

  // Derive expected chunk count from totalLen when totalChunks is zero
  // (forward-compat); otherwise prefer the wire value (catches off-by-one
  // sender bugs).
  size_t expectedChunks = ctrl.offer.totalChunks;
  if (expectedChunks == 0 && ctrl.offer.chunkSize > 0) {
    expectedChunks = (ctrl.offer.totalLen + ctrl.offer.chunkSize - 1) /
                     ctrl.offer.chunkSize;
  }

  resetBitmap(expectedChunks);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Target partition: firmware -> the inactive OTA slot; FS -> the live spiffs
  // partition (via the hook).
  const esp_partition_t* part =
      fsHooks_ ? static_cast<const esp_partition_t*>(fsHooks_->partition())
               : esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr) {
    sendResult(lamp_protocol::FwResultStatus::OtaBeginFail, 0xFF);
    state_ = State::Failed;
    return;
  }
  // The image (rounded up to a sector) must fit in the partition before erasing.
  const size_t kSector = SPI_FLASH_SEC_SIZE;
  const size_t numSectors =
      (static_cast<size_t>(offerTotalLen_) + kSector - 1u) / kSector;
  if (numSectors * kSector > part->size) {
    sendResult(lamp_protocol::FwResultStatus::OtaBeginFail, 0xFE);
    state_ = State::Failed;
    return;
  }

  // Widen the TWDT for the whole OTA window. The block-erase loop re-enables
  // interrupts between blocks so IDLE0 still runs (well within 30 s), and
  // per-chunk writes are sub-ms. The Arduino loop task stays unsubscribed.
  esp_task_wdt_config_t wdtWide = {
      .timeout_ms     = 30000,
      .idle_core_mask = (1u << 0),
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtWide);

  // Erase the entire image region now, synchronously, before arming the gate or
  // sending ACCEPT: the gate is disarmed and no ACCEPT has gone out, so no chunk
  // stream exists yet. This makes the recv path a pure write, which is what
  // stops cache+IRQ-off erase stalls from overflowing the ESP-NOW RX queue.
  // 64 KB block erase is ~5x faster than 4 KB sectors; the loop is hand-driven
  // to re-widen the IWDT before each block (a block holds interrupts off for its
  // whole duration and the tick_hook re-clamps the IWDT between blocks).
  {
    const uint32_t eraseT0 = millis();
    constexpr size_t kBlock = 64u * 1024u;
    const size_t eraseLen = numSectors * kSector;  // sector-aligned, covers image
    esp_err_t eraseErr = ESP_OK;
    size_t eoff = 0;
    while (eoff < eraseLen) {
      const size_t span = (eraseLen - eoff) < kBlock ? (eraseLen - eoff) : kBlock;
      widenIwdt();
      eraseErr = esp_partition_erase_range(part, eoff, span);
      if (eraseErr != ESP_OK) break;
      eoff += span;
    }
    widenIwdt();
    if (eraseErr != ESP_OK) {
#ifdef LAMP_DEBUG
      Serial.printf("[fw_receiver] upfront erase FAILED at off=%u err=0x%X\n",
                    (unsigned)eoff, (unsigned)eraseErr);
#endif
      sendResult(lamp_protocol::FwResultStatus::OtaBeginFail, 0xFD);
      restoreDefaultWdt();
      state_ = State::Failed;
      return;
    }
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] upfront erase: %u sectors in %u ms\n",
                  (unsigned)numSectors, (unsigned)(millis() - eraseT0));
#endif
  }
  erasedForLen_ = offerTotalLen_;  // coverage latch for verifyAndApply

  // nowMs was captured before this multi-second erase; re-read it so the
  // stall/no-progress timers below don't start already-expired (which would
  // fire a spurious stall-REQ for chunk 0).
  nowMs = millis();

  // Publish the partition pointer before arming the gate. Core 0 checks
  // publishedOtaHandle_ before touching publishedPartition_; the release-store
  // on arm pairs with Core 0's acquire-load.
  publishedPartition_.store(part, std::memory_order_release);
  publishedOtaHandle_.store(1, std::memory_order_release);
#ifdef LAMP_DEBUG
  Serial.printf("[fw_receiver] OFFER upfront-erase done: totalLen=%u sectors=%u "
                "part=0x%X\n",
                (unsigned)offerTotalLen_, (unsigned)numSectors,
                (unsigned)part->address);
#endif
#else
  // Native test: simulate the published-partition + armed-handle pair.
  publishedPartition_.store(reinterpret_cast<const esp_partition_t*>(0x1),
                            std::memory_order_release);
  publishedOtaHandle_.store(1, std::memory_order_release);
#endif

  streamingStartMs_ = nowMs;
  lastChunkMs_      = nowMs;  // gives the wisp 2s to start streaming
                              // before the first stall-REQ would fire
  lastChunkSeenMs_  = nowMs;  // and 60s before the no-progress abort fires
  lastReqMs_        = 0;
  // Reset both so the per-session "recv progress" log delta reflects this
  // session, not a prior failed attempt.
  recvChunksCount_    = 0;
  recvChunksLastLog_  = 0;
  // Indicator high-water survives a same-image restart so the bar doesn't
  // collapse and refill. Reset only when a genuinely different image is offered.
  if (offerTotalLen_ != progressForLen_) {
    recvProgress_.reset();
    progressForLen_ = offerTotalLen_;
  }
  state_            = State::Streaming;

  // Enter OTA quiet mode for the streaming window: suspends behaviors / draw /
  // non-OTA BLE writes. Mesh (EspNow) OTA also pauses the radio and kicks any
  // GATT client; BLE-pushed OTA keeps the connection (the phone is the chunk
  // transport) but still drops non-OTA writes. Exited in abortOta, the Failed
  // tick, and verifyAndApply.
  // Inter-session hold: skip enterQuiet when quietHeld_ is already set (a prior
  // session deferred its exit). Clear quietHoldUntilMs_ so the Idle tick won't
  // expire-and-exit mid-session.
  if (!quietHeld_) {
    ::lamp::ota_quiet_mode::enterQuiet(
        activeTransportKind_ == FirmwareTransportKind::EspNow);
    quietHeld_ = true;
  }
  quietHoldUntilMs_ = 0;

#ifdef LAMP_DEBUG
  Serial.printf("[fw_receiver] OFFER from %02X:%02X:%02X:%02X:%02X:%02X "
                "v=0x%08X totalLen=%u chunks=%u → ACCEPT\n",
                ctrl.sourceMac[0], ctrl.sourceMac[1], ctrl.sourceMac[2],
                ctrl.sourceMac[3], ctrl.sourceMac[4], ctrl.sourceMac[5],
                (unsigned)offerVersion_, (unsigned)offerTotalLen_,
                (unsigned)expectedChunks);
#endif

  // ACCEPT goes via ESP-NOW broadcast, which gets no MAC-layer ACK/retry, so
  // frame loss under BLE coex is real. Spread the burst (~1.6 s) so one ACCEPT
  // overlaps a WiFi-RX coex window; the sender drops dupes after the first.
  // First ACCEPT synchronous, the rest queued for tick() so the loop task (and
  // renderer) isn't blocked for the burst.
  sendAccept(ctrl, lamp_protocol::FwAcceptStatus::Accept);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  pendingAcceptCtrl_  = ctrl;
  pendingAcceptCount_ = kAcceptBurstCount - 1;
  nextAcceptMs_       = nowMs + kAcceptSpreadMs;
#endif
}

void FirmwareReceiver::onDoneOnLoop(const PendingFirmwareControl& ctrl,
                                    uint32_t /*nowMs*/) {
  if (state_ != State::Streaming) {
    // DONE in any other state is benign (late retry after verify+reboot, or a
    // DONE for a different flow). Ignore.
    return;
  }
  // DONE.version must match the accepted OFFER.
  if (ctrl.done.version != offerVersion_) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] DONE version mismatch (offer=0x%08X done=0x%08X)\n",
                  (unsigned)offerVersion_, (unsigned)ctrl.done.version);
#endif
    abortOta();
    sendResult(lamp_protocol::FwResultStatus::VersionMismatch, 0);
    state_ = State::Failed;
    return;
  }
  // Bitmap not yet full: REQ the first missing run and stay in Streaming. The
  // wisp fills the run and re-sends DONE.
  if (!isBitmapFull()) {
    const uint16_t missing = firstMissingChunk();
    if (missing != UINT16_MAX) {
      const uint16_t runLen = firstMissingRunLen(missing);
      sendReq(missing, runLen, lamp_protocol::FwReqReason::Gap);
    }
    return;
  }
#ifdef LAMP_DEBUG
  // Compare bitmap popcount against bitmapTotalChunks_ / recvChunksCount_. A
  // popcount below bitmapTotalChunks_ here means a race lost a bit-set
  // (isBitmapFull saw a torn byte and falsely returned true).
  {
    uint32_t popcount = 0;
    for (size_t i = 0; i < bitmap_.size(); ++i) {
      uint8_t b = bitmap_[i];
      while (b) { b &= (b - 1); ++popcount; }
    }
    Serial.printf("[fw_receiver] pre-verify: bitmap popcount=%u "
                  "totalChunks=%u recvChunksCount=%u\n",
                  (unsigned)popcount, (unsigned)bitmapTotalChunks_,
                  (unsigned)recvChunksCount_);
  }
#endif
  // verifyAndApply drives esp_ota_end + signature check + set_boot_partition +
  // esp_restart, returning the RESULT code; emit MSG_FW_RESULT and, on success,
  // pause before reboot so the broadcast clears the radio.
  state_ = State::Verify;
  const lamp_protocol::FwResultStatus rc = verifyAndApply();
  if (rc == lamp_protocol::FwResultStatus::Success) {
    state_ = State::Apply;
    sendResult(lamp_protocol::FwResultStatus::Success, 0);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // Pause so the broadcast leaves the radio before the CPU resets.
    delay(kPostResultPauseMs);
    // Exit quiet-mode before reboot OR before returning to Idle (the FS path
    // below returns without rebooting, so this is load-bearing there).
    ::lamp::ota_quiet_mode::exitQuiet();
    // FS image OTA finalizes without a reboot: SPIFFS is only read by the
    // onboarding webapp, so a remount makes the new UI live. Firmware OTA
    // reboots.
    if (fsHooks_ && fsHooks_->finalize) {
      fsHooks_->finalize();
      state_ = State::Idle;
      return;
    }
    esp_restart();
#endif
    return;
  }
  // Verify or apply failed. Surface the code and restore the WDT (the success
  // path reboots so it doesn't matter, but the fail path keeps running), then
  // drop to Failed for the tick() tombstone path.
  sendResult(rc, 0);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  restoreDefaultWdt();
#endif
  state_ = State::Failed;
}

void FirmwareReceiver::handleChunkOnRecvTask(const lamp_protocol::ParsedFwChunk& p) {
  // Bump writesInFlight_ before reading the armed gate so Core 1's
  // verifyAndApply barrier sees this write. The acquire fetch_add pairs with
  // Core 1's exchange(0, acq_rel) on publishedOtaHandle_: observing armed!=0
  // after the increment guarantees Core 1 observes the increment after its
  // disarm, so verify can't start while this write is still landing.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  writesInFlight_.fetch_add(1, std::memory_order_acquire);
  struct WritesInFlightGuard {
    std::atomic<int>* counter;
    ~WritesInFlightGuard() {
      counter->fetch_sub(1, std::memory_order_release);
    }
  };
  WritesInFlightGuard guard{&writesInFlight_};
#endif
  // publishedOtaHandle_ reads 0 once Core 1 clears the slot in teardown.
  const uint32_t armed = publishedOtaHandle_.load(std::memory_order_acquire);
  if (armed == 0) {
#ifdef LAMP_DEBUG
    // Rate-limited: at ~130 chunks/s of "not armed" this would flood the UART.
    // Throttle to once per second.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    static uint32_t lastUnarmedLogMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastUnarmedLogMs > 1000) {
      lastUnarmedLogMs = nowMs;
      Serial.printf("[fw_receiver] chunk drop: not armed (chunkIdx=%u)\n",
                    (unsigned)p.chunkIdx);
    }
#endif
#endif
    return;
  }
  // Bump the seen-stamp before any validation/write that may abort early; pairs
  // with Core 1's no-progress watchdog. Gated on source-MAC == active session
  // (wispMac_) so a rogue lamp blasting CHUNK frames can't keep the watchdog
  // falsely fresh while no real progress lands.
  if (std::memcmp(p.sourceMac, wispMac_, 6) == 0) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    lastChunkSeenMs_ = millis();
#endif
  }
  // offset + len must fit within the offer's totalLen, else a malformed chunk
  // could direct esp_partition_write past the erased range.
  if (p.offset + p.len > offerTotalLen_) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] chunk drop: oob chunkIdx=%u off=%u len=%u "
                  "total=%u\n",
                  (unsigned)p.chunkIdx, (unsigned)p.offset, (unsigned)p.len,
                  (unsigned)offerTotalLen_);
#endif
    return;
  }
  // Chunk size must match (last chunk may be shorter; intermediate must
  // be exactly FW_CHUNK_SIZE).
  if (p.len > offerChunkSize_) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] chunk drop: oversize chunkIdx=%u len=%u "
                  "max=%u\n",
                  (unsigned)p.chunkIdx, (unsigned)p.len,
                  (unsigned)offerChunkSize_);
#endif
    return;
  }
#ifdef LAMP_DEBUG
  // First chunk + every 256th, so chunk arrival is visible without flooding
  // the UART at 130 chunks/s.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (p.chunkIdx == 0 || (p.chunkIdx % 256) == 0) {
    Serial.printf("[fw_receiver] chunk recv chunkIdx=%u off=%u len=%u\n",
                  (unsigned)p.chunkIdx, (unsigned)p.offset, (unsigned)p.len);
  }
#endif
#endif
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Paired with Core 1's release-store of publishedOtaHandle_, the partition
  // pointer is valid here. Pure write: the region was erased upfront, and an
  // esp_partition_write of <=200 bytes holds cache/interrupts off for <1 ms,
  // far under the IWDT ceiling, so no per-chunk widen is needed.
  const esp_partition_t* part =
      publishedPartition_.load(std::memory_order_relaxed);
  if (part == nullptr) return;
  const esp_err_t err = esp_partition_write(part, p.offset, p.bytes, p.len);
  if (err != ESP_OK) {
    // Latch the write error for Core 1's stall watchdog. Don't send RESULT from
    // Core 0: broadcastRaw isn't WiFi-task-safe (the dedup ring + send queue
    // are Core 1 only). Core 1 sees the bitmap stop filling and the hard cap
    // fires PartitionWriteFail.
    return;
  }
#endif
  // markChunkReceived takes eraseMux_ around the byte RMW; Core 1's
  // isBitmapFull / firstMissingChunk take the same mux so they never see a torn
  // byte.
  markChunkReceived(p.chunkIdx);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Stamp last-chunk-ms for the stall watchdog (Core 1 reads it unsynchronised;
  // uint32_t read/write is atomic on 32-bit Xtensa). Bump the recv counter for
  // the throughput log.
  lastChunkMs_ = millis();
  recvChunksCount_++;
  recvProgress_.observe(uniqueChunks_);
#endif
}

void FirmwareReceiver::resetBitmap(size_t totalChunks) {
  bitmapTotalChunks_ = totalChunks;
  const size_t bytes = (totalChunks + 7) / 8;
  bitmap_.assign(bytes, 0);
  uniqueChunks_ = 0;
}

// All three bitmap accessors must take eraseMux_. markChunkReceived runs on
// Core 0 (WiFi recv) doing a non-atomic byte RMW (bitmap_[byteIdx] |= bit);
// isBitmapFull / firstMissingChunk run on Core 1 and walk the same vector.
// Xtensa LX6 per-core caches are weakly ordered with no per-byte atomicity, so
// without the mux two chunks sharing a byte can race-lose a bit, and Core 1 can
// read a torn byte and falsely conclude isBitmapFull while a bit is still 0:
// the sigverify-FAILED-with-complete-bitmap pattern, where verify runs with a
// chunk's bytes still 0xFF (erased, never written) inside the signed region.

void FirmwareReceiver::markChunkReceived(uint16_t chunkIdx) {
  const size_t byteIdx = chunkIdx / 8;
  if (byteIdx >= bitmap_.size()) return;
  const uint8_t bit = static_cast<uint8_t>(1u << (chunkIdx % 8));
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&eraseMux_);
#endif
  // Count only the 0->1 transition so uniqueChunks_ tracks true progress.
  // recvChunksCount_ counts every write (dups included) for throughput; the
  // progress bar must track uniques or it races past 100% in the recovery tail.
  if ((bitmap_[byteIdx] & bit) == 0) ++uniqueChunks_;
  bitmap_[byteIdx] |= bit;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&eraseMux_);
#endif
}

bool FirmwareReceiver::isBitmapFull() const {
  if (bitmapTotalChunks_ == 0) return false;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&eraseMux_);
#endif
  // Check all complete bytes are 0xFF, then handle the tail.
  const size_t fullBytes = bitmapTotalChunks_ / 8;
  for (size_t i = 0; i < fullBytes; ++i) {
    if (bitmap_[i] != 0xFF) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      portEXIT_CRITICAL(&eraseMux_);
#endif
      return false;
    }
  }
  const size_t tailBits = bitmapTotalChunks_ % 8;
  bool result;
  if (tailBits == 0) {
    result = true;
  } else {
    const uint8_t tailMask = static_cast<uint8_t>((1u << tailBits) - 1u);
    result = fullBytes < bitmap_.size() &&
             (bitmap_[fullBytes] & tailMask) == tailMask;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&eraseMux_);
#endif
  return result;
}

uint16_t FirmwareReceiver::firstMissingChunk() const {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&eraseMux_);
#endif
  uint16_t result = UINT16_MAX;
  for (size_t i = 0; i < bitmapTotalChunks_; ++i) {
    const size_t byteIdx = i / 8;
    const uint8_t bit = static_cast<uint8_t>(1u << (i % 8));
    if (byteIdx >= bitmap_.size()) break;
    if ((bitmap_[byteIdx] & bit) == 0) {
      result = static_cast<uint16_t>(i);
      break;
    }
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&eraseMux_);
#endif
  return result;
}

uint16_t FirmwareReceiver::firstMissingRunLen(uint16_t firstMissing) const {
  // Smallest count covering ALL missing chunks in a kMaxReqRunChunks window
  // from firstMissing (scan the window, return last-unset - first + 1), NOT the
  // longest contiguous run. Scattered single-chunk drops (the BLE-coex pattern)
  // would otherwise cost one round trip each; one windowed REQ re-streams the
  // span and catches them in a pass. Re-streamed dups are no-op NOR writes, far
  // cheaper than the saved round trips. Capped so an early all-missing session
  // doesn't dwarf the forward cursor.
  if (firstMissing == UINT16_MAX) return 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&eraseMux_);
#endif
  uint16_t lastMissingInWindow = firstMissing;  // first is missing by definition
  const size_t windowEnd =
      static_cast<size_t>(firstMissing) + kMaxReqRunChunks;
  for (size_t i = firstMissing;
       i < bitmapTotalChunks_ && i < windowEnd; ++i) {
    const size_t byteIdx = i / 8;
    const uint8_t bit = static_cast<uint8_t>(1u << (i % 8));
    if (byteIdx >= bitmap_.size()) break;
    if ((bitmap_[byteIdx] & bit) == 0) {
      lastMissingInWindow = static_cast<uint16_t>(i);
    }
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&eraseMux_);
#endif
  return static_cast<uint16_t>(lastMissingInWindow - firstMissing + 1);
}

bool FirmwareReceiver::sendAccept(const PendingFirmwareControl& ctrl,
                                  lamp_protocol::FwAcceptStatus status) {
  // ACCEPT goes back to the OFFER's source, not the in-flight flow's transport:
  // a DeclineBusy for a BLE app trying to start an OTA while a mesh OTA is
  // mid-flight must reach the app, not the wisp.
  FirmwareTransport* t = transportForKind(ctrl.transportKind);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_ACCEPT_FIXED_SIZE];
  // Reply at the version the OFFER arrived on so old-protocol senders can parse.
  const size_t n = lamp_protocol::buildFwAccept(
      buf, sizeof(buf), fwOutSeq_++, myMac_, ctrl.sourceMac,
      ctrl.seq, ctrl.offer.version, status, /*resumeOffset=*/0,
      ctrl.wireVersion,
      fsHooks_ ? fsHooks_->acceptType : lamp_protocol::MSG_FW_ACCEPT);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

bool FirmwareReceiver::sendReq(uint16_t firstChunkIdx, uint16_t chunkCount,
                               lamp_protocol::FwReqReason reason) {
  // REQ belongs to the in-flight flow; route to the active transport.
  FirmwareTransport* t = transportForKind(activeTransportKind_);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_REQ_FIXED_SIZE];
  const size_t n = lamp_protocol::buildFwReq(
      buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
      firstChunkIdx, chunkCount, reason, activeWireVersion_,
      fsHooks_ ? fsHooks_->reqType : lamp_protocol::MSG_FW_REQ);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

bool FirmwareReceiver::sendResult(lamp_protocol::FwResultStatus status,
                                  uint8_t detail) {
  // RESULT is the final ACK; same transport that streamed the chunks.
  FirmwareTransport* t = transportForKind(activeTransportKind_);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_RESULT_FIXED_SIZE];
  const size_t n = lamp_protocol::buildFwResult(
      buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
      status, detail, offerVersion_, activeWireVersion_,
      fsHooks_ ? fsHooks_->resultType : lamp_protocol::MSG_FW_RESULT);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

void FirmwareReceiver::abortOta() {
  // Clear the armed flag first so any in-flight Core 0 chunk write bails on the
  // gate check, then null the partition pointer. The release-store on disarm
  // pairs with Core 0's acquire-load.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  publishedOtaHandle_.store(0, std::memory_order_release);
  publishedPartition_.store(nullptr, std::memory_order_release);
  // Clear the erase-coverage latch so a re-OFFER must re-erase before verify.
  erasedForLen_ = 0;
  // Drop queued ACCEPT retries; the session is dead.
  pendingAcceptCount_ = 0;
  nextAcceptMs_       = 0;
  // No esp_ota_begin was called, so there's no handle to esp_ota_abort. The
  // erased region stays erased (the next OFFER re-erases anyway) and otadata is
  // untouched. Restore the wide WDT held across streaming.
  restoreDefaultWdt();
#else
  publishedOtaHandle_.store(0, std::memory_order_release);
  publishedPartition_.store(nullptr, std::memory_order_release);
#endif
  // Defer exitQuiet to keep the strip in indicator mode through a fast
  // multi-distributor handoff (the next OFFER can land within ms; avoid a
  // defaultColors flash). The Idle tick exits for real if no retry arrives
  // within kInterSessionQuietHoldMs.
  if (quietHeld_) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    quietHoldUntilMs_ = millis() + kInterSessionQuietHoldMs;
#else
    quietHoldUntilMs_ = kInterSessionQuietHoldMs;
#endif
  }
}

lamp_protocol::FwResultStatus FirmwareReceiver::verifyAndApply() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Streaming is over (chunk floods stopped before DONE). Restore the normal
  // WDT; esp_partition_read + verify don't hammer the bus, so 5 s is safe.
  restoreDefaultWdt();
  // Disarm the gate so no late Core 0 chunk races verify/apply. exchange(0) is
  // atomic but not synchronous: a handler already past the gate-check is still
  // in flight. writesInFlight_ below is the synchronous barrier.
  const uint32_t armed =
      publishedOtaHandle_.exchange(0, std::memory_order_acq_rel);
  const esp_partition_t* otaPartition =
      publishedPartition_.exchange(nullptr, std::memory_order_acq_rel);
  if (armed == 0 || otaPartition == nullptr) {
    return lamp_protocol::FwResultStatus::OtaEndFail;
  }
  // Wait for any in-flight Core 0 chunk write to finish. A handler that read
  // armed!=0 just before the exchange is still in esp_partition_write; without
  // this wait, esp_partition_read races it and reads pre-write bytes, so the
  // SHA-256 mismatches and ed25519 verify FAILS despite a complete bitmap (the
  // "sigverify FAILED with full bitmap" bug). Yield rather than spin.
  {
    uint32_t waitMs = 0;
    while (writesInFlight_.load(std::memory_order_acquire) > 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
      waitMs += 2;
      if (waitMs > 5000) {
#ifdef LAMP_DEBUG
        Serial.printf("[fw_receiver] verify: writesInFlight_ stuck non-zero "
                      "after 5s, proceeding anyway\n");
#endif
        break;  // Bail rather than deadlock; surface the sigverify fail instead.
      }
    }
#ifdef LAMP_DEBUG
    if (waitMs > 0) {
      Serial.printf("[fw_receiver] verify: drained %u ms of in-flight "
                    "Core 0 writes before read\n", (unsigned)waitMs);
    }
#endif
  }
  // Erase-coverage check: erasedForLen_ is set only after every block erase
  // succeeded, and a failed-erase OFFER never armed the gate. A mismatch here
  // means a re-OFFER changed the image length without re-erasing; fail loud
  // rather than boot a partial image.
#ifdef LAMP_DEBUG
  Serial.printf("[fw_receiver] verify: erasedForLen=%u offerTotalLen=%u\n",
                (unsigned)erasedForLen_, (unsigned)offerTotalLen_);
#endif
  if (erasedForLen_ != offerTotalLen_) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] verify: erase-coverage mismatch (%u != %u) → fail\n",
                  (unsigned)erasedForLen_, (unsigned)offerTotalLen_);
#endif
    return lamp_protocol::FwResultStatus::PartitionWriteFail;
  }
  // FS-image OTA: the hook mounts the freshly-written spiffs partition,
  // recomputes the manifest digest, ed25519-verifies against fw.lsig, and checks
  // the version. No boot-partition flip (the partition is read at runtime, not
  // booted). Post-write verification, so a bad image is caught here but the old
  // UI is already gone (the accepted no-A/B-rollback tradeoff). Firmware OTA
  // falls through to the streaming LSIG verify + boot flip below.
  if (fsHooks_) {
    return fsHooks_->verify(static_cast<const void*>(otaPartition),
                            offerVersion_);
  }
  // Streaming verify: the lamp's ~280 KB heap can't hold a 1.4 MB image, so feed
  // firmware_signature's streaming reader from the OTA partition via
  // esp_partition_read (~4 KB stack per call vs 1.4 MB heap). otaPartition is
  // captured by value; it was swapped out of the atomic above, so no Core 0
  // chunk can race.
  auto reader = [otaPartition](size_t offset, size_t wantBytes,
                               uint8_t* out) -> int {
    const esp_err_t err = esp_partition_read(
        otaPartition, offset, out, wantBytes);
    if (err != ESP_OK) return -1;
    return static_cast<int>(wantBytes);
  };
  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  const bool ok = firmware::verifySignedFirmware(reader, offerTotalLen_,
                                                 &outChannel, &outVersion);
  if (!ok) {
#ifdef LAMP_DEBUG
    Serial.println("[fw_receiver] signature verify FAILED");
#endif
    return lamp_protocol::FwResultStatus::SignatureFail;
  }
  if (outVersion != offerVersion_) {
    // Footer version disagrees with the offer's claim; the bytes don't match
    // the offered version.
    return lamp_protocol::FwResultStatus::OfferShaMismatch;
  }
  // Re-check the verified footer's channel before flipping the boot partition.
  // The OFFER-time channelMatchesOurs already rejected cross-variant binaries;
  // this closes the gap against a CI asset swap or a future path that bypasses
  // the OFFER gate.
  const char* ours = FIRMWARE_CHANNEL_STR ? FIRMWARE_CHANNEL_STR : "";
  if (!outChannel || std::strcmp(outChannel, ours) != 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] type-gate REJECT: footer channel=\"%s\" "
                  "ours=\"%s\"\n",
                  outChannel ? outChannel : "(null)", ours);
#endif
    return lamp_protocol::FwResultStatus::OfferShaMismatch;
  }
  // Set boot to the inactive slot. Next reboot lands PENDING_VERIFY; the
  // post-boot self-health path in lamp.cpp (g_pendingVerify / mark_app_valid)
  // handles the health check.
  const esp_err_t bootErr = esp_ota_set_boot_partition(otaPartition);
  if (bootErr != ESP_OK) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] set_boot_partition failed: 0x%X\n", (unsigned)bootErr);
#endif
    return lamp_protocol::FwResultStatus::SetBootFail;
  }
  return lamp_protocol::FwResultStatus::Success;
#else
  // Native test path: the test rig mirrors the state machine in its own class
  // and never calls this directly. Host tests cover bitmap + state transitions,
  // not the partition-read + signature pass. Returning Success keeps an
  // accidental host path from regressing into a hard fail.
  publishedOtaHandle_.store(0, std::memory_order_relaxed);
  return lamp_protocol::FwResultStatus::Success;
#endif
}

}  // namespace lamp
