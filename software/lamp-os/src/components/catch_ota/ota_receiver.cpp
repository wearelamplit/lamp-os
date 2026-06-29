#include "ota_receiver.hpp"

#include <cstring>
#include <vector>

#include "espnow_link.hpp"
#include "ota_signature.hpp"
#include "radio_quiesce.hpp"
#include "rollback_breaker.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <spi_flash_mmap.h>  // SPI_FLASH_SEC_SIZE
// Low-level IWDT (Interrupt-Watchdog) control. A W25Q block erase can hold
// interrupts off for 50-750 ms under cache-disable bursts, well past the
// IWDT stage timeout that the IDF tick_hook re-applies every FreeRTOS tick.
// We widen the stage timeout right before each block erase using the
// "not public api" hal surface, the least-bad option until upstream exposes
// a runtime IWDT reconfigure call.
#include "hal/wdt_hal.h"
#include "hal/mwdt_ll.h"
#include "soc/timer_group_reg.h"
#endif

namespace catch_ota {
namespace ota_receiver {

namespace {

// The only firmware channel this catch module will boot. The verified LSIG
// footer's channel string must match exactly before we flip the boot
// partition — the cryptographic + type gate, since we no longer drop
// cross-channel OFFERs up front (discovery is via no-TLV HELLO).
constexpr char kExpectChannel[] = "standard-stable";

// Streaming budgets / cadences.
constexpr uint32_t kChunkStallReqMs    = 2000;    // emit REQ after 2s gap
constexpr uint32_t kStreamingHardCapMs = 600000;  // total OTA budget (10 min)
constexpr uint32_t kNoProgressAbortMs  = 60000;   // 1 min no-chunk abort
constexpr uint32_t kPostResultPauseMs  = 100;     // pre-restart delay

// ACCEPT burst: the first ACCEPT goes out synchronously from onOffer; the
// remaining (kAcceptBurstCount - 1) are spread from tick() so a single
// dropped ACCEPT during the radio-mode-settle window doesn't strand the
// handshake. Quenched the instant the first chunk lands.
constexpr uint8_t  kAcceptBurstCount = 5;
constexpr uint32_t kAcceptSpreadMs   = 400;

// Per-REQ run cap: one REQ spans first..last-missing within this many chunks
// so scattered single-chunk drops recover in one round trip. 20 = one sector.
constexpr uint16_t kMaxReqRunChunks = 20;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// IWDT widening for each block erase.
constexpr uint32_t kIwdtEraseStageMs = 8000;   // stage 0 (interrupt) widen
constexpr uint32_t kIwdtEraseResetMs = 16000;  // stage 1 (system reset) widen
constexpr uint32_t kIwdtTicksPerUs   = 500;    // matches IDF's IWDT_TICKS_PER_US

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

// Restore the default TWDT timeout when an OTA flow exits. Matches the
// sdkconfig-baked pre-OTA configuration (5 s, IDLE0 subscribed). We do NOT
// subscribe the loop task — arduino-esp32 leaves loopTaskWDTEnabled=false and
// never resets it from the loop body, so subscribing would guarantee a panic.
void restoreDefaultWdt() {
  esp_task_wdt_config_t wdtDefault = {
      .timeout_ms     = 5000,
      .idle_core_mask = (1u << 0),
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtDefault);
}

// Post-commit failure recovery. radioEnterOtaMode() tore down softAP/BLE at the
// committed OFFER and no failure path restores main's normal radio — the lamp is
// left with no web-config AP until a manual power-cycle. Reboot instead: a
// clean boot brings back the AP, ch11 discovery, and BLE. Loop-safety comes
// from the breaker — every committed attempt is counted at the commit point,
// so a deterministically-failing image gives up after kMaxAttempts. The short
// settle lets the in-flight ESP-NOW RESULT frame actually transmit first.
[[noreturn]] void rebootAfterOtaFailure() {
  delay(150);
  esp_restart();
  for (;;) {
  }
}
#endif

// --- Lifecycle state ---------------------------------------------------------

enum class State : uint8_t {
  Idle      = 0,
  Streaming = 1,  // partition erased; chunks flowing
  Verify    = 2,  // signature check in progress
  Apply     = 3,  // boot partition set; awaiting reboot
  Failed    = 4,  // terminal error; reset to Idle on next tick
};

State state_ = State::Idle;

// Snapshot of the in-flight OFFER.
uint8_t  wispMac_[6]        = {0};  // OFFER source; target of ACCEPT/REQ/RESULT
uint8_t  myMac_[6]          = {0};  // our MAC; source of ACCEPT/REQ/RESULT
uint16_t offerSeq_          = 0;    // OFFER.seq — echoed in ACCEPT
uint32_t offerVersion_      = 0;
uint32_t offerTotalLen_     = 0;
uint16_t offerChunkSize_    = 0;

// Sequence counter for our outbound FW frames. Fresh seq per send.
uint16_t fwOutSeq_ = 0;

// Chunk-received bitmap. One bit per chunk; set means received.
std::vector<uint8_t> bitmap_;
size_t bitmapTotalChunks_ = 0;

// Timeouts (wall-clock millis):
//   lastChunkMs_      — last successful chunk write (drives stall-REQ)
//   lastChunkSeenMs_  — last chunk that arrived at all (drives no-progress abort)
//   lastReqMs_        — last REQ sent (rate-limit)
//   streamingStartMs_ — when we entered Streaming (hard-cap budget)
uint32_t lastChunkMs_      = 0;
uint32_t lastChunkSeenMs_  = 0;
uint32_t lastReqMs_        = 0;
uint32_t streamingStartMs_ = 0;

// ACCEPT burst drained from tick().
uint8_t  acceptBurstRemaining_ = 0;
uint32_t nextAcceptMs_         = 0;

// First-chunk-arrived signal (quenches the ACCEPT burst + bench diagnostic).
uint32_t recvChunksCount_ = 0;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
// Active OTA partition pointer (plain member — single-threaded).
const esp_partition_t* otaPartition_ = nullptr;

// Erase-coverage latch: set to offerTotalLen_ only after every block erase
// returned ESP_OK; checked in verifyAndApply before flipping the boot
// partition. A re-OFFER at a different length without re-erasing fails loud
// instead of booting a partial image.
uint32_t erasedForLen_ = 0;
#endif

// --- Bitmap helpers ----------------------------------------------------------

void resetBitmap(size_t totalChunks) {
  bitmapTotalChunks_ = totalChunks;
  bitmap_.assign((totalChunks + 7) / 8, 0);
}

void markChunkReceived(uint16_t chunkIdx) {
  const size_t byteIdx = chunkIdx / 8;
  if (byteIdx >= bitmap_.size()) return;
  bitmap_[byteIdx] |= static_cast<uint8_t>(1u << (chunkIdx % 8));
}

bool isBitmapFull() {
  if (bitmapTotalChunks_ == 0) return false;
  const size_t fullBytes = bitmapTotalChunks_ / 8;
  for (size_t i = 0; i < fullBytes; ++i) {
    if (bitmap_[i] != 0xFF) return false;
  }
  const size_t tailBits = bitmapTotalChunks_ % 8;
  if (tailBits == 0) return true;
  const uint8_t tailMask = static_cast<uint8_t>((1u << tailBits) - 1u);
  return fullBytes < bitmap_.size() &&
         (bitmap_[fullBytes] & tailMask) == tailMask;
}

uint16_t firstMissingChunk() {
  for (size_t i = 0; i < bitmapTotalChunks_; ++i) {
    const size_t byteIdx = i / 8;
    if (byteIdx >= bitmap_.size()) break;
    if ((bitmap_[byteIdx] & static_cast<uint8_t>(1u << (i % 8))) == 0) {
      return static_cast<uint16_t>(i);
    }
  }
  return UINT16_MAX;
}

// Smallest count covering ALL missing chunks within a kMaxReqRunChunks window
// from firstMissing — one REQ reclaims a cluster of scattered drops instead
// of one round trip per hole. NOT longest-contiguous-run.
uint16_t firstMissingRunLen(uint16_t firstMissing) {
  if (firstMissing == UINT16_MAX) return 0;
  uint16_t lastMissingInWindow = firstMissing;  // first is missing by definition
  const size_t windowEnd = static_cast<size_t>(firstMissing) + kMaxReqRunChunks;
  for (size_t i = firstMissing; i < bitmapTotalChunks_ && i < windowEnd; ++i) {
    const size_t byteIdx = i / 8;
    if (byteIdx >= bitmap_.size()) break;
    if ((bitmap_[byteIdx] & static_cast<uint8_t>(1u << (i % 8))) == 0) {
      lastMissingInWindow = static_cast<uint16_t>(i);
    }
  }
  return static_cast<uint16_t>(lastMissingInWindow - firstMissing + 1);
}

// --- Outbound send helpers ---------------------------------------------------

bool emitAccept(const uint8_t tgtMac[6], uint16_t echoSeq, uint32_t echoVersion,
                FwAcceptStatus status) {
  uint8_t buf[FW_ACCEPT_FIXED_SIZE];
  const size_t n = buildFwAccept(buf, sizeof(buf), fwOutSeq_++, myMac_, tgtMac,
                                 echoSeq, echoVersion, status,
                                 /*resumeOffset=*/0);
  if (!n) return false;
  return espnowSend(tgtMac, buf, n);
}

bool sendReq(uint16_t firstChunkIdx, uint16_t chunkCount, FwReqReason reason) {
  uint8_t buf[FW_REQ_FIXED_SIZE];
  const size_t n = buildFwReq(buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
                              firstChunkIdx, chunkCount, reason);
  if (!n) return false;
  return espnowSend(wispMac_, buf, n);
}

bool sendResult(FwResultStatus status, uint8_t detail) {
  uint8_t buf[FW_RESULT_FIXED_SIZE];
  const size_t n = buildFwResult(buf, sizeof(buf), fwOutSeq_++, myMac_, wispMac_,
                                 status, detail, offerVersion_);
  if (!n) return false;
  return espnowSend(wispMac_, buf, n);
}

// --- Abort + verify ----------------------------------------------------------

void abortOta() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  otaPartition_ = nullptr;
  // Clear the erase-coverage latch so a re-OFFER must re-erase before verify
  // will accept the assembled image. No esp_ota_begin was called (we drove the
  // partition prep ourselves), so there's no handle to abort; the erased
  // region stays erased and otadata is untouched.
  erasedForLen_ = 0;
  // The wide WDT was held active across streaming; restore it.
  restoreDefaultWdt();
#endif
  acceptBurstRemaining_ = 0;
  nextAcceptMs_         = 0;
}

FwResultStatus verifyAndApply() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Streaming is over (chunk floods stopped before DONE) — restore the normal
  // WDT; esp_partition_read + signature verify don't hammer the bus.
  restoreDefaultWdt();
  if (otaPartition_ == nullptr) {
    return FwResultStatus::OtaEndFail;
  }
  // Erase-coverage check: erasedForLen_ is set only after every block erase
  // returned ESP_OK. A mismatch here means a re-OFFER changed the image length
  // without re-erasing — fail loud rather than flip to a partial image.
  if (erasedForLen_ != offerTotalLen_) {
#ifdef LAMP_DEBUG
    Serial.printf("[catch_ota.rx] verify: erase-coverage mismatch (%u != %u)\n",
                  (unsigned)erasedForLen_, (unsigned)offerTotalLen_);
#endif
    return FwResultStatus::PartitionWriteFail;
  }
  // Streaming verify: the lamp can't fit a ~1.4 MB image in heap, so feed the
  // signature reader straight from the partition in ascending blocks (~4 KB of
  // stack inside ota_signature). otaPartition_ is stable for this call —
  // single-threaded, no chunk can race the read.
  const esp_partition_t* part = otaPartition_;
  auto reader = [part](size_t offset, size_t wantBytes, uint8_t* out) -> int {
    if (esp_partition_read(part, offset, out, wantBytes) != ESP_OK) return -1;
    return static_cast<int>(wantBytes);
  };
  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  if (!verifySignedFirmware(reader, offerTotalLen_, &outChannel, &outVersion)) {
#ifdef LAMP_DEBUG
    Serial.println("[catch_ota.rx] signature verify FAILED");
#endif
    return FwResultStatus::SignatureFail;
  }
  if (outVersion != offerVersion_) {
    // Footer version disagrees with the OFFER's claim — the bytes don't
    // correspond to the offered version.
    return FwResultStatus::OfferShaMismatch;
  }
  // Type gate: the verified footer channel must be exactly ours before we flip
  // the boot partition. This is the gate (no OFFER-time channel drop anymore).
  if (!outChannel || std::strcmp(outChannel, kExpectChannel) != 0) {
#ifdef LAMP_DEBUG
    Serial.printf("[catch_ota.rx] type-gate REJECT: footer channel=\"%s\" "
                  "expect=\"%s\"\n",
                  outChannel ? outChannel : "(null)", kExpectChannel);
#endif
    return FwResultStatus::OfferShaMismatch;
  }
  // LAST: flip the boot partition. Only reached after a full verify pass.
  if (esp_ota_set_boot_partition(part) != ESP_OK) {
    return FwResultStatus::SetBootFail;
  }
  return FwResultStatus::Success;
#else
  return FwResultStatus::Success;
#endif
}

}  // namespace

// =============================================================================
// Public entry points (all on the loop task)
// =============================================================================

void onOffer(const ParsedFwOffer& offer, const uint8_t devMac[6]) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Register the OFFER source as a unicast peer so ACCEPT/REQ/RESULT can be
  // sent back to it (declines included).
  espnowAddPeer(devMac);
  // Snapshot our STA MAC up front: the sender matches ACCEPT.sourceMac against
  // its offer target, so even a decline must carry the real source MAC.
  esp_wifi_get_mac(WIFI_IF_STA, myMac_);
#endif

  // Chunk size is locked to FW_CHUNK_SIZE; we don't negotiate. DeclineBusy
  // gives the distributor a clear "won't accept" so it stops retrying.
  if (offer.chunkSize != FW_CHUNK_SIZE) {
    emitAccept(devMac, offer.seq, offer.version, FwAcceptStatus::DeclineBusy);
    return;
  }

  // Already mid-flow → single-source-at-a-time. An idempotent re-OFFER (same
  // version, same source) re-ACKs so a distributor that missed our first
  // ACCEPT can restart cleanly; anything else is DeclineBusy.
  if (state_ == State::Streaming || state_ == State::Verify) {
    const bool sameSource = std::memcmp(devMac, wispMac_, 6) == 0;
    if (offer.version == offerVersion_ && sameSource) {
      emitAccept(devMac, offer.seq, offer.version, FwAcceptStatus::Accept);
    } else {
      emitAccept(devMac, offer.seq, offer.version, FwAcceptStatus::DeclineBusy);
    }
    return;
  }

  // New flow commits here. Receiving starts — drop the softAP + BLE so ESP-NOW
  // owns the radio for the transfer (the lamp ran web-controllable up to now).
  // The teardown's mode switch clears the ESP-NOW peer table, so re-register the
  // sender as a unicast peer before any ACCEPT/REQ/RESULT goes out.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  radioEnterOtaMode();
  espnowReinit();
  espnowAddPeer(devMac);
#endif
  // Count the attempt at the commit point, not after a successful erase: any
  // post-commit failure below reboots to restore the radio, so the breaker must
  // have already counted this attempt or a deterministic failure loops forever.
  // Re-OFFERs of an in-progress session hit the busy/re-ACK path above and
  // never reach here, so this still fires exactly once per genuine attempt.
  recordAttempt(offer.sha256Prefix);

  // Begin a new flow. Snapshot the OFFER first.
  std::memcpy(wispMac_, devMac, 6);
  offerSeq_       = offer.seq;
  offerVersion_   = offer.version;
  offerTotalLen_  = offer.totalLen;
  offerChunkSize_ = offer.chunkSize;

  size_t expectedChunks = offer.totalChunks;
  if (expectedChunks == 0 && offer.chunkSize > 0) {
    expectedChunks = (offer.totalLen + offer.chunkSize - 1) / offer.chunkSize;
  }
  resetBitmap(expectedChunks);

  uint32_t nowMs = 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  esp_wifi_get_mac(WIFI_IF_STA, myMac_);

  // Pick the inactive OTA partition.
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr) {
    sendResult(FwResultStatus::OtaBeginFail, 0xFF);
#ifdef LAMP_DEBUG
    Serial.println("[catch_ota.rx] commit FAIL: no update partition → reboot");
#endif
    rebootAfterOtaFailure();
  }
  // Bound check: the image (sector-rounded) must fit before we erase the span.
  const size_t kSector = SPI_FLASH_SEC_SIZE;
  const size_t numSectors =
      (static_cast<size_t>(offerTotalLen_) + kSector - 1u) / kSector;
  if (numSectors * kSector > part->size) {
    sendResult(FwResultStatus::OtaBeginFail, 0xFE);
#ifdef LAMP_DEBUG
    Serial.println("[catch_ota.rx] commit FAIL: image overruns partition "
                   "→ reboot");
#endif
    rebootAfterOtaFailure();
  }

  // Widen the TWDT for the whole OTA window. IDLE0 still runs between block
  // erases (interrupts re-enabled) and per-chunk writes are sub-millisecond,
  // so 30 s is ample. The loop task is NOT TWDT-subscribed.
  esp_task_wdt_config_t wdtWide = {
      .timeout_ms     = 30000,
      .idle_core_mask = (1u << 0),
      .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdtWide);

  // Upfront full-region erase: erase the ENTIRE image region NOW, before
  // ACCEPT goes out. Nothing is in flight yet (the distributor hasn't been
  // ACCEPTed), so the recv path that follows is a pure write — no per-chunk
  // erase. 64 KB block granularity uses the chip's faster block-erase opcode;
  // we re-widen the IWDT before each block because a block holds interrupts
  // off for its whole duration.
  {
    constexpr size_t kBlock = 64u * 1024u;
    const size_t eraseLen = numSectors * kSector;
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
      Serial.printf("[catch_ota.rx] upfront erase FAILED at off=%u err=0x%X\n",
                    (unsigned)eoff, (unsigned)eraseErr);
#endif
      sendResult(FwResultStatus::OtaBeginFail, 0xFD);
      restoreDefaultWdt();
      rebootAfterOtaFailure();
    }
  }
  otaPartition_ = part;
  erasedForLen_ = offerTotalLen_;  // coverage latch for verifyAndApply
  nowMs = millis();
#ifdef LAMP_DEBUG
  Serial.printf("[catch_ota.rx] OFFER v=0x%08X totalLen=%u chunks=%u "
                "→ erased, ACCEPT\n",
                (unsigned)offerVersion_, (unsigned)offerTotalLen_,
                (unsigned)expectedChunks);
#endif
#endif

  streamingStartMs_ = nowMs;
  lastChunkMs_      = nowMs;  // 2s grace before the first stall-REQ
  lastChunkSeenMs_  = nowMs;  // 60s grace before the no-progress abort
  lastReqMs_        = 0;
  recvChunksCount_  = 0;

  // Set Streaming BEFORE ACCEPT so the opening chunks pass the state guard.
  state_ = State::Streaming;

  // First ACCEPT synchronously; the rest spread from tick().
  emitAccept(wispMac_, offerSeq_, offerVersion_, FwAcceptStatus::Accept);
  acceptBurstRemaining_ = kAcceptBurstCount - 1;
  nextAcceptMs_         = nowMs + kAcceptSpreadMs;
}

void onChunkOnLoop(const ParsedFwChunk& chunk) {
  // Drop any chunk outside an armed session.
  if (state_ != State::Streaming) return;

  // Source-MAC gate: only the active distributor's chunks keep this session's
  // no-progress watchdog fresh, so a rogue lamp can't perpetually revive it.
  if (std::memcmp(chunk.sourceMac, wispMac_, 6) == 0) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    lastChunkSeenMs_ = millis();
#endif
  }

  // Bounds: offset + len must fit within the offered length; len must not
  // exceed the chunk size (last chunk may be shorter).
  if (chunk.offset + chunk.len > offerTotalLen_) return;
  if (chunk.len > offerChunkSize_) return;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (otaPartition_ == nullptr) return;
  // Pure write — the region was erased up front, so the recv path never erases.
  if (esp_partition_write(otaPartition_, chunk.offset, chunk.bytes,
                          chunk.len) != ESP_OK) {
    // Leave the bit unset; the stall watchdog will REQ it and the distributor
    // re-sends. We don't send RESULT from here.
    return;
  }
#endif

  // Dedup is inherent: re-served chunks (new seq) hit an already-set bit and
  // re-write idempotently. We key on chunkIdx, never on seq.
  markChunkReceived(chunk.chunkIdx);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  lastChunkMs_ = millis();
#endif
  recvChunksCount_++;
}

void onDone(const ParsedFwDone& done) {
  if (state_ != State::Streaming) return;

  // Version sanity: DONE.version must match the OFFER we accepted.
  if (done.version != offerVersion_) {
    abortOta();
    sendResult(FwResultStatus::VersionMismatch, 0);
    state_ = State::Failed;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    rebootAfterOtaFailure();
#endif
    return;
  }
  // Bitmap not full → REQ the first missing run and stay Streaming; the
  // distributor fills the run and re-sends DONE.
  if (!isBitmapFull()) {
    const uint16_t missing = firstMissingChunk();
    if (missing != UINT16_MAX) {
      sendReq(missing, firstMissingRunLen(missing), FwReqReason::Gap);
    }
    return;
  }

  state_ = State::Verify;
  const FwResultStatus rc = verifyAndApply();
  if (rc == FwResultStatus::Success) {
    state_ = State::Apply;
    sendResult(FwResultStatus::Success, 0);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    delay(kPostResultPauseMs);  // let the RESULT leave the radio before reset
    esp_restart();
#endif
    return;
  }
  // Verify/apply failed. verifyAndApply already restored the WDT; surface the
  // failure, then reboot to restore main's radio (post-commit, so the AP is
  // down until a clean boot).
  sendResult(rc, 0);
  state_ = State::Failed;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  rebootAfterOtaFailure();
#endif
}

void tick(uint32_t nowMs) {
  switch (state_) {
    case State::Idle:
    case State::Apply:
    case State::Verify:
      return;

    case State::Failed:
      state_ = State::Idle;
      return;

    case State::Streaming:
      break;
  }

  // ACCEPT burst: quench once a chunk has arrived (proof an ACCEPT got
  // through), else drain one per tick at the spread cadence.
  if (acceptBurstRemaining_ != 0 && recvChunksCount_ != 0) {
    acceptBurstRemaining_ = 0;
  }
  if (acceptBurstRemaining_ != 0 &&
      static_cast<int32_t>(nowMs - nextAcceptMs_) >= 0) {
    emitAccept(wispMac_, offerSeq_, offerVersion_, FwAcceptStatus::Accept);
    --acceptBurstRemaining_;
    nextAcceptMs_ = nowMs + kAcceptSpreadMs;
  }

  // Hard cap: abort and report a stall sentinel. Signed delta for the same
  // reason as the no-progress check below — onOffer's upfront erase stamps
  // streamingStartMs_ on a clock ~4s ahead of tick()'s captured nowMs, so an
  // unsigned delta underflows to ~4.29e9 and fires the cap instantly.
  const int32_t elapsedStart = static_cast<int32_t>(nowMs - streamingStartMs_);
  if (elapsedStart > static_cast<int32_t>(kStreamingHardCapMs)) {
    abortOta();
    sendResult(FwResultStatus::PartitionWriteFail, 0xFE);
    state_ = State::Idle;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    rebootAfterOtaFailure();
#endif
    return;
  }

  // No-progress: nothing arrived for kNoProgressAbortMs → drop to Idle so we
  // resume HELLOs and any peer can re-OFFER. Signed delta guards a chunk
  // handler bumping lastChunkSeenMs_ a few ms past our captured nowMs.
  const int32_t elapsedSeen = static_cast<int32_t>(nowMs - lastChunkSeenMs_);
  if (lastChunkSeenMs_ != 0 &&
      elapsedSeen > static_cast<int32_t>(kNoProgressAbortMs)) {
    abortOta();
    state_ = State::Idle;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    rebootAfterOtaFailure();
#endif
    return;
  }

  // Stall watchdog: 2s since the last chunk → one REQ for the lowest missing
  // run, rate-limited.
  if (lastChunkMs_ != 0 && (nowMs - lastChunkMs_) > kChunkStallReqMs &&
      (lastReqMs_ == 0 || (nowMs - lastReqMs_) > kChunkStallReqMs)) {
    const uint16_t firstMissing = firstMissingChunk();
    if (firstMissing != UINT16_MAX) {
      sendReq(firstMissing, firstMissingRunLen(firstMissing),
              FwReqReason::StallWatchdog);
      lastReqMs_ = nowMs;
    }
  }
}

bool isInProgress() {
  return state_ != State::Idle && state_ != State::Failed;
}

}  // namespace ota_receiver
}  // namespace catch_ota
