#include "firmware_receiver.hpp"

#include <cstring>

#include "firmware_signature.hpp"
#include "../network/ble_control.hpp"  // pauseRadioForOta / resumeRadioAfterOta
#include "core/ota_quiet_mode.hpp"     // enterQuiet / exitQuiet
#include "../network/show_receiver.hpp"
#include "../../version.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <spi_flash_mmap.h>  // SPI_FLASH_SEC_SIZE
// Low-level IWDT (Interrupt-Watchdog) control. We need to widen the IWDT's
// stage timeouts around each block erase in the upfront partition erase,
// because a W25Q erase can take 50-750 ms under cache-disable bursts while
// the IWDT's configured stage timeout is shorter — which fires `rst:0x8
// (TG1WDT_SYS_RESET)` mid-erase if we don't widen first.
//
// We DO set CONFIG_ESP_INT_WDT_TIMEOUT_MS=1000 via platformio.ini's
// custom_sdkconfig (see [env:upesy_wroom]), which raises the baseline.
// That alone isn't sufficient: the IDF tick_hook re-applies the
// baked-in timeout literal on every FreeRTOS tick (its
// wdt_hal_config_stage call uses CONFIG_ESP_INT_WDT_TIMEOUT_MS as a
// compile-time constant), so a runtime erase that exceeds the original
// budget can still trip the watchdog between ticks. We use the
// hal/wdt_hal.h "not public api" surface to widen the stage timeout
// mid-erase, then restore it. Least-bad option until upstream exposes
// a runtime IWDT reconfigure API.
#include "hal/wdt_hal.h"
#include "hal/mwdt_ll.h"
#include "soc/timer_group_reg.h"
#endif

namespace lamp {

namespace {

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// Restore the default TWDT timeout when an OTA flow exits. Matches the
// sdkconfig-baked pre-OTA configuration (5 s timeout, IDLE0 subscribed).
// Critically: we do NOT subscribe the loop task — arduino-esp32 leaves
// `loopTaskWDTEnabled = false` by default (see
// framework-arduinoespressif32/cores/esp32/main.cpp), so the Arduino loop
// body never calls esp_task_wdt_reset(). Subscribing the loop task to
// the TWDT under those conditions guarantees a panic the moment the
// configured timeout elapses, regardless of how busy the loop is.
//
// Called from every flow-exit path (abort, OtaBeginFail, verifyAndApply).
void restoreDefaultWdt() {
  esp_task_wdt_config_t wdtDefault = {
      .timeout_ms     = 5000,
      .idle_core_mask = (1u << 0),  // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtDefault);
}
#endif

// Streaming budgets / cadences.
constexpr uint32_t kChunkStallReqMs    = 2000;   // emit REQ after 2s gap
constexpr uint32_t kStreamingHardCapMs = 600000; // total OTA budget (10 min)
// If no chunk arrives for this long, the offerer has likely failed or gone
// away. Abort the local Streaming state so we drop back to Idle and start
// broadcasting HELLOs again, letting either the same peer re-OFFER or any
// other higher-version peer take over. Without this, a fleet ends up with
// receivers permanently locked to dead offerers until the 5-minute hard
// cap fires — a long deadlock window during which the receiver is invisible
// to every other potential source.
constexpr uint32_t kNoProgressAbortMs  = 60000;  // 1 min no-chunk abort
constexpr uint32_t kPostResultPauseMs  = 100;    // pre-restart delay

// ACCEPT burst spread tunables. The first ACCEPT goes out synchronously
// from onOfferOnLoop; the remaining (kAcceptBurstCount - 1) are
// scheduled at kAcceptSpreadMs intervals and drained from tick(). The
// total spread (1.6 s at 5 × 400 ms) easily overlaps 8+ BLE adv
// intervals on the sender so at least one ACCEPT lands in a
// WiFi-RX-allowed coex window. Quenched as soon as we receive our
// first chunk — that's empirical proof an earlier ACCEPT got through.
constexpr uint8_t  kAcceptBurstCount   = 5;
constexpr uint32_t kAcceptSpreadMs     = 400;

// Full-upfront erase: the whole image region is erased once on Core 1 in
// onOfferOnLoop before the gate arms, so each chunk arrival on Core 0 is a
// pure esp_partition_write — no erase on the recv path.

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// IWDT widening for the entire OTA window. The prebuilt arduino-esp32
// libs bake in CONFIG_ESP_INT_WDT_TIMEOUT_MS=300; the tick_hook in IDF's
// int_wdt.c re-applies that 300 ms ceiling every FreeRTOS tick. A W25Q
// sector erase can take 50-750 ms (chip variability + cache-disable
// burst tails) and would trip IWDT mid-erase.
//
// Strategy: re-widen on the OFFER path AND on every chunk handler. The
// tick_hook keeps clobbering us back to 300 ms between widens, but we
// replace it again before each erase — net effect is the IWDT timeout
// sits at our wide value for the cache-disabled erase window. There
// IS a sub-millisecond race window where the tick_hook can clamp
// between our widenIwdt() and `esp_partition_erase_range` actually
// disabling interrupts; safe in practice because (a) the CONFIG_
// ESP_INT_WDT_TIMEOUT_MS=1000 baseline (custom_sdkconfig) already
// gives 1 s of slack — enough for a worst-case 750 ms erase — and
// (b) interrupts go off the instant the erase actually starts, so
// the tick_hook can't clamp once we're inside the cache-disabled
// window.
//
// `mwdt_dev = &TIMERG1` matches int_wdt.c's `IWDT_INSTANCE=WDT_MWDT1`
// for ESP32 (the H/W on TG1 is the IWDT; TG0 holds the task watchdog).
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

// =============================================================================
// Lifecycle
// =============================================================================

void FirmwareReceiver::begin(FirmwareTransport* meshTransport,
                             FirmwareTransport* bleTransport) {
  meshTransport_ = meshTransport;
  bleTransport_  = bleTransport;
  // Snapshot the lamp's MAC from whichever transport is wired (both should
  // report the same identity — it's the lamp's chip MAC, transport-agnostic).
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
      // Inter-session quiet hold expiry: a prior session deferred its
      // exitQuiet (see abortOta / State::Failed) so the strip doesn't
      // flash defaultColors between back-to-back OFFERs from different
      // distributors in a 3+ lamp mesh. quietHoldUntilMs_ == 0 is the
      // sentinel — without the `!= 0` guard, `nowMs >= 0` is always
      // true and any Idle tick would exitQuiet prematurely.
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
      // Queued ACCEPT retries. Drain at most one per tick — the spread
      // is the whole point — and quench the queue as soon as a chunk
      // arrives (the ACCEPT clearly got through). recvChunksCount_
      // is reset to 0 at session start (see onOfferOnLoop), so a
      // non-zero value here means we've received our first chunk.
      if (pendingAcceptCount_ != 0 && recvChunksCount_ != 0) {
        pendingAcceptCount_ = 0;
      }
      if (pendingAcceptCount_ != 0 &&
          static_cast<int32_t>(nowMs - nextAcceptMs_) >= 0) {
        sendAccept(pendingAcceptCtrl_, lamp_protocol::FwAcceptStatus::Accept);
        --pendingAcceptCount_;
        nextAcceptMs_ = nowMs + kAcceptSpreadMs;
      }

      // No erase work here — the whole image region was erased upfront in
      // onOfferOnLoop, so Core 0 chunk writes are pure (no per-chunk erase
      // stall). The OTA converges via REQs for any RF-dropped chunks.
#endif
      // Hard cap: 60s budget from Accepted → DONE-fully-applied. Beyond
      // that, we abort and report PartitionWriteFail with a sentinel
      // detail byte so the wisp can distinguish "stream stalled" from
      // an actual flash write error.
      if (nowMs - streamingStartMs_ > kStreamingHardCapMs) {
#ifdef LAMP_DEBUG
        Serial.println("[fw_receiver] streaming hard cap exceeded, aborting");
#endif
        abortOta();
        sendResult(lamp_protocol::FwResultStatus::PartitionWriteFail, 0xFE);
        state_ = State::Idle;
        return;
      }
      // No-progress timeout: if no chunks have arrived for kNoProgressAbortMs,
      // the offerer has likely failed or moved out of range. Abort so we
      // drop back to Idle and start broadcasting HELLOs again, letting any
      // peer (the same one re-trying, or a different higher-version one)
      // offer fresh. Without this, fleet-wide OTA stalls on the first lost
      // session per receiver — the receiver suppresses HELLO in Streaming,
      // so no other peer can find it again until the 5-minute hard cap.
      //
      // Gated on lastChunkSeenMs_ (any arrival, including failed writes)
      // rather than lastChunkMs_ (successful write). A stream where writes
      // are temporarily failing but chunks keep arriving is NOT a dead
      // session — the per-chunk stall watchdog will REQ the holes and the
      // sender will resend. Killing the session here on a write-failure
      // streak is what aborted in-flight OTAs on first observation.
      //
      // Signed cast because Core 0's chunk handler races our tick-entry
      // nowMs across the vTaskDelay yield in the background-erase block
      // above — it can bump lastChunkSeenMs_ to a value a few ms PAST
      // our captured nowMs, and unsigned subtraction wraps to ~4 billion
      // (> 60000 → spurious abort).
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
      // Throughput diagnostic: every 256 newly-received chunks, log the
      // running total. Compare against wisp's "stream progress" log to
      // localise where chunks are getting lost (RF vs. wisp throughput).
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
      // Reset to Idle on the next tick after a failure. The Failed state
      // is just a one-tick latch so tests can observe the failure had
      // occurred without us immediately flipping back to Idle.
      state_ = State::Idle;
      // Defense in depth: most failure paths route through abortOta()
      // which already deferred the exit. The verify-fail branch in
      // handleDoneOnLoop sets State::Failed directly; defer the exit
      // here too so the strip stays in indicator mode through a quick
      // multi-distributor handoff. Idle tick expires the hold.
      if (quietHeld_) {
        quietHoldUntilMs_ = nowMs + kInterSessionQuietHoldMs;
      }
      return;
    }

    case State::Verify:
      // These are transient or synchronously-handled in
      // handleControlOnLoop. tick() doesn't drive transitions out of them.
      return;
  }
}

// =============================================================================
// Inbound control plane (Core 1)
// =============================================================================

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
  // Channel mismatch → silent drop. No ACCEPT, no RESULT. Per the locked
  // scope decision: cross-channel offers are a wisp-side bug (its target-
  // picker should filter by channel against HELLO inventory) and shouldn't
  // generate ack-stream noise on the air.
  if (!channelMatchesOurs(ctrl.offer.channel)) {
#ifdef LAMP_DEBUG
    char ch[lamp_protocol::FW_CHANNEL_LEN + 1] = {0};
    std::memcpy(ch, ctrl.offer.channel, lamp_protocol::FW_CHANNEL_LEN);
    Serial.printf("[fw_receiver] OFFER channel=%s ours=%s → silent drop\n",
                  ch, FIRMWARE_CHANNEL_STR);
#endif
    return;
  }
  // Chunk size mismatch → decline-busy (we don't negotiate; v1 is locked
  // to 200). Could equally well be a silent drop, but DeclineBusy gives
  // the wisp a clear "won't accept this" signal so it stops retrying.
  if (ctrl.offer.chunkSize != lamp_protocol::FW_CHUNK_SIZE) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] OFFER chunkSize=%u != %u, declining\n",
                  (unsigned)ctrl.offer.chunkSize,
                  (unsigned)lamp_protocol::FW_CHUNK_SIZE);
#endif
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineBusy);
    return;
  }
  // Version <= ours → already-current decline. The wisp moves the lamp
  // out of its "needs update" set.
  if (ctrl.offer.version <= FIRMWARE_VERSION) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] OFFER v=0x%08X <= ours=0x%08X, decline-current\n",
                  (unsigned)ctrl.offer.version, (unsigned)FIRMWARE_VERSION);
#endif
    sendAccept(ctrl, lamp_protocol::FwAcceptStatus::DeclineAlreadyCurrent);
    return;
  }
  // Already mid-flow → enforce single-source-at-a-time. An idempotent
  // re-OFFER (same version AND same transport AND same source identity)
  // re-ACKs so a wisp that missed our first ACCEPT can restart cleanly.
  // Anything else (different version, different transport — e.g. BLE
  // app trying to interrupt a mesh OTA in progress — or different source
  // on the same transport) is DeclineBusy. The mutex prevents two
  // concurrent flows writing the same OTA partition.
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

  // Begin a new OTA flow. Full-upfront erase: erase the ENTIRE image region
  // synchronously here on Core 1 before arming the gate or sending ACCEPT, so
  // the recv path is a pure write (no erase) — see the file-scope comment.
  //
  // Sequencing on the Core 1 OFFER path:
  //   1. Snapshot ALL OFFER fields into members FIRST (so Core 0 sees
  //      offerTotalLen_/offerChunkSize_ when the gate arms).
  //   2. Resize bitmap_.
  //   3. Pick partition; widen the WDTs; erase the whole region in 64 KB
  //      blocks; latch erasedForLen_.
  //   4. Publish partition pointer, then arm the gate via release-store.
  //   5. Send the first ACCEPT synchronously; queue the rest of the ACCEPT
  //      burst for tick() to drain (don't block the loop task with vTaskDelay).

  // (1) Snapshot OFFER fields BEFORE anything that could be observed by
  // Core 0. Critically, offerTotalLen_ is read by handleChunkOnRecvTask's
  // bounds check — if it's still zero when a chunk arrives, the bounds
  // check passes spuriously and we silently drop writes past the offer's
  // declared length.
  std::memcpy(wispMac_, ctrl.sourceMac, 6);
  // Snapshot transport context for the active flow — the busy-check on
  // any subsequent OFFER uses these to enforce single-source semantics.
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

  // (2) Init the chunk bitmap.
  resetBitmap(expectedChunks);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Pick the inactive OTA partition.
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr) {
    sendResult(lamp_protocol::FwResultStatus::OtaBeginFail, 0xFF);
    state_ = State::Failed;
    return;
  }
  // Bound check: the image (rounded up to a sector) must fit in the
  // partition before we erase that whole span.
  const size_t kSector = SPI_FLASH_SEC_SIZE;
  const size_t numSectors =
      (static_cast<size_t>(offerTotalLen_) + kSector - 1u) / kSector;
  if (numSectors * kSector > part->size) {
    sendResult(lamp_protocol::FwResultStatus::OtaBeginFail, 0xFE);
    state_ = State::Failed;
    return;
  }

  // Widen the TWDT for the whole OTA window. IDLE0 must still run at least
  // every 30 s; the block-erase loop re-enables interrupts between blocks
  // (so IDLE0 is serviced) and per-chunk writes are sub-millisecond, so
  // 30 s is ample. We do NOT subscribe the Arduino loop task to the TWDT
  // (arduino-esp32 leaves loopTaskWDTEnabled=false and never resets it from
  // the loop body), so a busy loop task can't trip it.
  esp_task_wdt_config_t wdtWide = {
      .timeout_ms     = 30000,
      .idle_core_mask = (1u << 0),
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtWide);

  // Full-upfront erase: erase the entire image region NOW, synchronously on
  // Core 1, before the gate is armed and before ACCEPT goes out. The gate is
  // disarmed (publishedOtaHandle_==0) so Core 0 drops any chunk until we
  // finish — and the sender hasn't been ACCEPTed, so there's no stream to
  // drop yet. The recv path that follows is then a pure write (no erase),
  // which is what stops the cache+IRQ-off erase stalls from overflowing the
  // ESP-NOW RX queue mid-stream.
  //
  // 64 KB block granularity: esp_partition_erase_range issues the chip's
  // block-erase opcode for 64 KB-aligned/sized ranges (~5x faster than 4 KB
  // sector erases). We drive the loop ourselves so we can re-widen the IWDT
  // before each block — a block holds interrupts off for its whole duration,
  // and the IDF tick_hook re-clamps the IWDT to its baked-in ceiling whenever
  // interrupts are briefly on between blocks.
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

  // Publish partition pointer FIRST, then arm the gate. Core 0 checks
  // publishedOtaHandle_ before touching publishedPartition_. The release-
  // store on arm pairs with Core 0's acquire-load.
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
  // recvChunksCount_ is cumulative-across-boot in name but the per-session
  // log delta (recvChunksLastLog_ ≥ 256 trigger) needs both to reset so
  // the "recv progress: N chunks received" line reflects THIS session's
  // count rather than carrying over from a prior failed attempt.
  recvChunksCount_    = 0;
  recvChunksLastLog_  = 0;
  // Indicator progress high-water survives a same-image restart so the bar
  // doesn't collapse to 0 and refill (the flashing). Reset it only when a
  // genuinely different image is offered.
  if (offerTotalLen_ != progressForLen_) {
    recvProgress_.reset();
    progressForLen_ = offerTotalLen_;
  }
  state_            = State::Streaming;

  // Enter OTA quiet mode for the duration of the streaming window.
  // Suspends behaviors / expression draw / non-OTA BLE writes; if this
  // is a mesh OTA (EspNow) the radio is also paused and any connected
  // GATT client is kicked. For BLE-pushed OTA we keep the GATT
  // connection alive — the phone IS the chunk transport — but we still
  // silently drop writes to non-OTA characteristics so the user can't
  // fiddle with controls mid-upload. Exited in abortOta() (failure
  // paths), the Failed-state tombstone in tick(), and at the end of
  // verifyAndApply() (success path).
  //
  // Inter-session hold: skip enterQuiet when quietHeld_ is already
  // true (a previous session deferred its exit and we're inside the
  // hold window). Clear quietHoldUntilMs_ so the Idle tick won't
  // expire-and-exit while this new session is live.
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

  // ACCEPT goes via ESP-NOW broadcast; broadcast frames don't get MAC-
  // layer ACK + retry, so individual frame loss under BLE coex
  // contention is real. Spread kAcceptBurstCount ACCEPTs across
  // kAcceptBurstCount * kAcceptSpreadMs (~1.6 s) so at least one
  // overlaps a WiFi-RX-allowed coex window on the sender. The sender's
  // onAccept state-guard silently drops dupes after the first.
  //
  // First ACCEPT goes out synchronously; the rest are queued for tick()
  // to drain at kAcceptSpreadMs intervals so the loop task isn't blocked
  // for the burst duration freezing the renderer.
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
    // DONE arriving in any other state is benign; ignore. Could be a
    // late retry from the wisp after we already verified+rebooted, or
    // a DONE for a different OTA flow.
    return;
  }
  // Version sanity: DONE.version must match the OFFER we accepted.
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
  // Bitmap not yet full → REQ the first missing run and stay in
  // Streaming. The wisp will fill the run and re-send DONE.
  if (!isBitmapFull()) {
    const uint16_t missing = firstMissingChunk();
    if (missing != UINT16_MAX) {
      const uint16_t runLen = firstMissingRunLen(missing);
      sendReq(missing, runLen, lamp_protocol::FwReqReason::Gap);
    }
    return;
  }
#ifdef LAMP_DEBUG
  // Diagnostic before verify: compare the bitmap popcount against
  // recvChunksCount_ and bitmapTotalChunks_. If popcount <
  // bitmapTotalChunks_ here we have a race-induced lost bit-set
  // (isBitmapFull observed a torn byte and falsely returned true).
  // recvChunksCount_ is the on-the-wire arrival count (incremented
  // unconditionally per chunk after a successful partition_write);
  // popcount is what isBitmapFull / firstMissingChunk read. The two
  // should match if bitmap RMWs aren't being lost.
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
  // Verify + apply. verifyAndApply() drives esp_ota_end + signature
  // check + esp_ota_set_boot_partition + esp_restart. It returns the
  // RESULT status code so we can emit MSG_FW_RESULT and (on success)
  // pause briefly before reboot for the broadcast to clear the radio.
  state_ = State::Verify;
  const lamp_protocol::FwResultStatus rc = verifyAndApply();
  if (rc == lamp_protocol::FwResultStatus::Success) {
    state_ = State::Apply;
    sendResult(lamp_protocol::FwResultStatus::Success, 0);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    // Pause so the broadcast leaves the radio before the CPU resets.
    delay(kPostResultPauseMs);
    // Symmetry — exit quiet-mode before reboot. The chip restarts
    // milliseconds later so the lamp wouldn't see the un-quiet state
    // anyway, but a future code path that returns from success without
    // rebooting won't leave the lamp stranded in quiet-mode.
    ::lamp::ota_quiet_mode::exitQuiet();
    esp_restart();
#endif
    return;
  }
  // Verify or apply failed. Surface the failure code, restore the WDT
  // (verifyAndApply widened it; the success path reboots so it doesn't
  // matter, but the fail path leaves us running and we must put the
  // watchdog back), then drop to Failed for the tick() tombstone path.
  sendResult(rc, 0);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  restoreDefaultWdt();
#endif
  state_ = State::Failed;
}

// =============================================================================
// Inbound chunk plane (Core 0 — WiFi recv task)
// =============================================================================

void FirmwareReceiver::handleChunkOnRecvTask(const lamp_protocol::ParsedFwChunk& p) {
  // Bump writesInFlight_ BEFORE reading the armed gate so Core 1's
  // verifyAndApply barrier sees us. If we increment after reading armed,
  // Core 1 could observe armed=disarmed AND writesInFlight_=0 between
  // our load and increment, miss us in the wait-loop, and start verify
  // while our write is still landing. The fetch_add acquire fence
  // pairs with Core 1's exchange(0, acq_rel) on publishedOtaHandle_:
  // if we observe armed nonzero AFTER incrementing, Core 1 will observe
  // our increment after its disarm.
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
  // Drop chunks while not streaming. The published armed-flag is the
  // gate: 0 means "no OTA in progress" — Core 1 cleared the slot during
  // teardown.
  const uint32_t armed = publishedOtaHandle_.load(std::memory_order_acquire);
  if (armed == 0) {
    // Even unarmed-drop counts as "the air still has chunks for us" — but
    // there's no session to keep alive, so just leave the log and return.
#ifdef LAMP_DEBUG
    // Rate-limited: at ~130 chunks/s of "not armed" we'd flood the UART.
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
  // Chunk arrived AND we're armed for a session — bump the seen-stamp
  // now, BEFORE any subsequent validation / erase / write that may
  // abort early. Pairs with the no-progress watchdog on Core 1 (see
  // tick()). Gated on source-MAC == active-session source (wispMac_)
  // so a rogue/buggy lamp blasting CHUNK frames at us can't keep this
  // session's watchdog perpetually fresh while no real progress lands.
  if (std::memcmp(p.sourceMac, wispMac_, 6) == 0) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    lastChunkSeenMs_ = millis();
#endif
  }
  // Bounds check: offset + len must fit within the offer's totalLen.
  // Without this guard, a malformed chunk could direct esp_partition_write
  // past the erased range.
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
  // First chunk + every 256th — confirms chunks are actually arriving without
  // flooding the UART at 130 chunks/s.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (p.chunkIdx == 0 || (p.chunkIdx % 256) == 0) {
    Serial.printf("[fw_receiver] chunk recv chunkIdx=%u off=%u len=%u\n",
                  (unsigned)p.chunkIdx, (unsigned)p.offset, (unsigned)p.len);
  }
#endif
#endif
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Paired with the release-store of publishedOtaHandle_ on Core 1; we
  // have a valid partition pointer by the time we get here.
  const esp_partition_t* part =
      publishedPartition_.load(std::memory_order_relaxed);
  if (part == nullptr) return;
  // Pure write — the whole image region was erased upfront in onOfferOnLoop
  // before the gate armed, so the recv path never erases. esp_partition_write
  // of <=200 bytes disables cache/interrupts for <1 ms, far under the IWDT
  // ceiling, so no per-chunk widen is needed.
  const esp_err_t err = esp_partition_write(part, p.offset, p.bytes, p.len);
  if (err != ESP_OK) {
    // Latch the write error so Core 1's stall watchdog can pick it up.
    // We don't send RESULT from Core 0 — broadcastRaw isn't WiFi-task-
    // safe in this codebase (the dedup ring + send queue are designed
    // around Core 1 calls). Core 1 will notice the bitmap stops filling
    // and the hard-cap timer will fire, sending PartitionWriteFail.
    return;
  }
#endif
  // markChunkReceived takes eraseMux_ around the byte RMW; Core 1's
  // isBitmapFull() / firstMissingChunk() take the same mux around their
  // reads so they see a consistent picture and never a torn byte.
  markChunkReceived(p.chunkIdx);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Stamp last-chunk-ms for the stall watchdog. Core 1 reads this
  // unsynchronised; uint32_t read/write is atomic on 32-bit Xtensa.
  lastChunkMs_ = millis();
  // Throughput diagnostic — bump a recv counter so Core 1's tick can
  // periodically log "lamp received N chunks" to compare against the
  // wisp's stream-progress log. uint32_t atomic on Xtensa.
  recvChunksCount_++;
  recvProgress_.observe(uniqueChunks_);
#endif
}

// =============================================================================
// Bitmap helpers
// =============================================================================

void FirmwareReceiver::resetBitmap(size_t totalChunks) {
  bitmapTotalChunks_ = totalChunks;
  const size_t bytes = (totalChunks + 7) / 8;
  bitmap_.assign(bytes, 0);
  uniqueChunks_ = 0;
}

// All three bitmap accessors must take eraseMux_ — markChunkReceived
// runs on Core 0 (WiFi recv task) and does a non-atomic byte RMW
// (`bitmap_[byteIdx] |= bit`); isBitmapFull / firstMissingChunk run on
// Core 1 (loop task) and walk the same vector. Xtensa LX6's per-core
// data caches have weak ordering and no per-byte atomicity guarantee —
// without the mux, two chunks whose indices share a byte can race RMW
// and silently lose a bit, AND Core 1 can observe a torn byte mid-RMW
// and falsely conclude isBitmapFull() while a bit is still 0. The
// latter is the smoking-gun sigverify-FAILED-with-complete-bitmap
// pattern: the receiver advances to verifyAndApply with a chunk's
// worth of bytes still 0xFF (erased, never written) inside the signed
// region. eraseMux_ (IRQ-disabled, scoped tightly to the byte op) serializes
// this bitmap RMW against the const accessors.

void FirmwareReceiver::markChunkReceived(uint16_t chunkIdx) {
  const size_t byteIdx = chunkIdx / 8;
  if (byteIdx >= bitmap_.size()) return;
  const uint8_t bit = static_cast<uint8_t>(1u << (chunkIdx % 8));
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&eraseMux_);
#endif
  // Count the 0→1 transition only, so uniqueChunks_ is true unique progress.
  // recvChunksCount_ counts every write (dups included) and is throughput
  // diagnostics; the progress bar must track uniques or it races past 100%
  // during the dup-heavy recovery tail. Same mux as the RMW.
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
  // Returns the smallest count value that asks the distributor for ALL
  // missing chunks within a kMaxReqRunChunks-wide window starting at
  // firstMissing. Concretely: scan chunks [first, first+window), find
  // the LAST unset bit, return (last - first + 1).
  //
  // This is NOT "longest contiguous run". When chunks are scattered
  // (e.g., the BLE-coex single-chunk-drop pattern observed on bench:
  // 6559, 6563, 6578, 6580, 6590, 6591, 6595, 6600, 6605 all missing),
  // a contiguous scan returns 1 and we pay one round trip per missing
  // chunk — at ~130 chunks/s burst rate and 8 missing per cycle that's
  // 8 round trips to net 8 chunks. The window approach returns 47
  // (last missing = 6605, first = 6559), one REQ asks the distributor
  // to re-stream 6559..6605, the receiver catches all 8 misses in one
  // pass. The distributor's smart-REQ path streams exactly `count`
  // chunks then jumps back to forward progress — already-received
  // duplicates in the span are re-written to flash (NOR 0→0 / 1→1 is
  // a no-op physical write, just a few µs of bus time). The waste of
  // re-streaming 39 already-received chunks is dramatically less than
  // the savings of 7 round trips at ~50 ms each.
  //
  // Capped at kMaxReqRunChunks so an early-stream "everything missing"
  // session doesn't dwarf the forward-stream cursor.
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

// =============================================================================
// Outbound send helpers
// =============================================================================

bool FirmwareReceiver::sendAccept(const PendingFirmwareControl& ctrl,
                                  lamp_protocol::FwAcceptStatus status) {
  // ACCEPT goes back to the source of the OFFER, NOT the in-flight flow's
  // transport — if a BLE app is trying to start an OTA and we're busy
  // mid-mesh, the DeclineBusy needs to reach the app, not the wisp.
  FirmwareTransport* t = transportForKind(ctrl.transportKind);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_ACCEPT_FIXED_SIZE];
  // Reply at the version the OFFER came in on so old-protocol senders
  // can process our ACCEPT.
  const size_t n = lamp_protocol::buildFwAccept(
      buf, sizeof(buf), fwOutSeq_++, myMac_, ctrl.sourceMac,
      ctrl.seq, ctrl.offer.version, status, /*resumeOffset=*/0,
      ctrl.wireVersion);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

bool FirmwareReceiver::sendReq(uint16_t firstChunkIdx, uint16_t chunkCount,
                               lamp_protocol::FwReqReason reason) {
  // REQ is part of the in-flight flow — route to the active transport.
  FirmwareTransport* t = transportForKind(activeTransportKind_);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_REQ_FIXED_SIZE];
  const size_t n = lamp_protocol::buildFwReq(
      buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
      firstChunkIdx, chunkCount, reason, activeWireVersion_);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

bool FirmwareReceiver::sendResult(lamp_protocol::FwResultStatus status,
                                  uint8_t detail) {
  // RESULT is the final ACK at end of the flow — same transport that
  // streamed the chunks.
  FirmwareTransport* t = transportForKind(activeTransportKind_);
  if (!t) return false;
  uint8_t buf[lamp_protocol::FW_RESULT_FIXED_SIZE];
  const size_t n = lamp_protocol::buildFwResult(
      buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
      status, detail, offerVersion_, activeWireVersion_);
  if (!n) return false;
  return t->sendFrame(buf, n);
}

// =============================================================================
// Abort + verify
// =============================================================================

void FirmwareReceiver::abortOta() {
  // Clear the armed flag FIRST so any in-flight Core 0 chunk write
  // bails on the gate check. Then null the partition pointer. The
  // release-store on disarm pairs with the acquire-load on Core 0 so
  // the partition pointer can be safely cleared afterwards.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  publishedOtaHandle_.store(0, std::memory_order_release);
  publishedPartition_.store(nullptr, std::memory_order_release);
  // Clear the erase-coverage latch so a re-OFFER must re-erase the region
  // before verify will accept the assembled image.
  erasedForLen_ = 0;
  // Drop any queued ACCEPT retries — the session is dead, no point
  // spreading more ACCEPTs at an offerer that's also given up.
  pendingAcceptCount_ = 0;
  nextAcceptMs_       = 0;
  // Because we drove the partition prep ourselves (no esp_ota_begin was
  // called), there is no esp_ota_handle_t to esp_ota_abort. The erased
  // region stays erased; the next OFFER re-erases the whole region anyway.
  // otadata is untouched (only esp_ota_set_boot_partition flips it).
  //
  // The wide WDT was held active across streaming so the loop task
  // wouldn't trip while Core 0 hammered flash writes; restore now.
  restoreDefaultWdt();
#else
  publishedOtaHandle_.store(0, std::memory_order_release);
  publishedPartition_.store(nullptr, std::memory_order_release);
#endif
  // Defer exitQuiet — keep the strip in indicator mode through a quick
  // multi-distributor handoff (the very next OFFER from a different
  // peer can land within milliseconds and we don't want a defaultColors
  // flash between). Idle tick expires the hold and exits quiet for
  // real if no retry arrives within kInterSessionQuietHoldMs.
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
  // Streaming phase is over (chunk floods stopped before DONE arrived) —
  // restore the normal WDT timeout. esp_partition_read + signature
  // verify don't hammer the bus the way the chunk plane did, so a 5s
  // window is safe.
  restoreDefaultWdt();
  // Disarm the gate so no late Core 0 chunk can race verify/apply.
  // exchange(0) is atomic but NOT synchronous — any Core 0 chunk handler
  // that already passed the gate-check is still in flight. The
  // writesInFlight_ counter below is the synchronous barrier.
  const uint32_t armed =
      publishedOtaHandle_.exchange(0, std::memory_order_acq_rel);
  const esp_partition_t* otaPartition =
      publishedPartition_.exchange(nullptr, std::memory_order_acq_rel);
  if (armed == 0 || otaPartition == nullptr) {
    return lamp_protocol::FwResultStatus::OtaEndFail;
  }
  // Barrier: wait for any in-flight Core 0 chunk write to complete. A
  // chunk handler that read armed=nonzero microseconds before our
  // exchange is still executing esp_partition_write somewhere. Without
  // this wait, esp_partition_read below races the write — the verify
  // streamer reads pre-write bytes for that region, sha256 doesn't
  // match what the signer hashed, ed25519 verify FAILS even though the
  // bitmap honestly reports complete and every chunk landed correctly.
  // This was the smoking-gun bug for "sigverify FAILED with full
  // bitmap". Yield rather than tight-spin while the last few Core 0
  // writes drain (pure esp_partition_writes — the region was erased
  // upfront, so no per-write erase).
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
        break;  // Safety valve — shouldn't fire; if it does we'd rather
                // surface the sigverify fail than deadlock forever.
      }
    }
#ifdef LAMP_DEBUG
    if (waitMs > 0) {
      Serial.printf("[fw_receiver] verify: drained %u ms of in-flight "
                    "Core 0 writes before read\n", (unsigned)waitMs);
    }
#endif
  }
  // Erase-coverage check (recast for full-upfront erase): erasedForLen_ is set
  // ONLY after every block erase in onOfferOnLoop returned ESP_OK, and an
  // OFFER whose erase failed never armed the gate. Reaching verify with a
  // mismatch means a re-OFFER changed the image length without re-erasing —
  // fail loud instead of flipping the boot pointer to a partial image.
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
  // Streaming verify: the lamp's ~280 KB free heap can't
  // fit a 1.4 MB firmware image, so we feed firmware_signature's
  // streaming reader API straight from the OTA partition via
  // esp_partition_read. The reader pulls bytes in 4 KB blocks into a
  // stack-local buffer inside firmware_signature.cpp; total RAM
  // footprint for the verify pass is ~4 KB of stack instead of 1.4 MB
  // of heap. Producer side: scripts/sign_firmware.py signs SHA-256 of
  // the signed region (not the raw region), so the ed25519 verify
  // operates on a constant-size 32-byte digest computed by streaming
  // mbedtls SHA-256 in firmware_signature.cpp. The LSIG byte layout
  // on disk is unchanged.
  //
  // We capture otaPartition by value in the lambda; it's a stable
  // pointer for the duration of the verify call (we already swapped
  // it out of the atomic above, so no Core 0 chunk can race).
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
    // Footer version disagrees with the offer's claim. Treat as offer-
    // sha-mismatch (the firmware bytes don't correspond to the offered
    // version).
    return lamp_protocol::FwResultStatus::OfferShaMismatch;
  }
  // Defense-in-depth: the OFFER-time channelMatchesOurs already rejected
  // cross-variant binaries before any chunks transferred. Re-check the
  // verified footer's channel string against our own here, before we
  // flip the boot partition — closes the gap against (a) a CI asset
  // swap that makes it through the app's check, (b) a future code path
  // that bypasses the OFFER gate.
  const char* ours = FIRMWARE_CHANNEL_STR ? FIRMWARE_CHANNEL_STR : "";
  if (!outChannel || std::strcmp(outChannel, ours) != 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] type-gate REJECT: footer channel=\"%s\" "
                  "ours=\"%s\"\n",
                  outChannel ? outChannel : "(null)", ours);
#endif
    return lamp_protocol::FwResultStatus::OfferShaMismatch;
  }
  // Set boot partition to the inactive slot. Next reboot lands in the
  // new firmware in PENDING_VERIFY state; the post-boot self-health
  // path in lamp.cpp (g_pendingVerify / mark_app_valid) handles the
  // health check.
  const esp_err_t bootErr = esp_ota_set_boot_partition(otaPartition);
  if (bootErr != ESP_OK) {
#ifdef LAMP_DEBUG
    Serial.printf("[fw_receiver] set_boot_partition failed: 0x%X\n", (unsigned)bootErr);
#endif
    return lamp_protocol::FwResultStatus::SetBootFail;
  }
  return lamp_protocol::FwResultStatus::Success;
#else
  // Native test path: the test rig mirrors the state machine in its own
  // class (see test_firmware_receiver/firmware_receiver.cpp) and never
  // calls into this verifyAndApply directly. The production receiver is
  // arduino/IDF-only — host tests cover bitmap + state transitions, not
  // the partition-read + signature pass. Returning Success keeps any
  // accidental host path from regressing into a hard fail.
  publishedOtaHandle_.store(0, std::memory_order_relaxed);
  return lamp_protocol::FwResultStatus::Success;
#endif
}

}  // namespace lamp
