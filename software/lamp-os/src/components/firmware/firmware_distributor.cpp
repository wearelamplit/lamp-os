#include "firmware_distributor.hpp"

#include <cstring>

#include "firmware_receiver.hpp"  // FirmwareTransport interface
#include "firmware_signature.hpp"  // kLsigFooterLen
#include "../network/ble_control.hpp"  // pauseRadioForOta / resumeRadioAfterOta
#include "core/ota_quiet_mode.hpp"     // enterQuiet / exitQuiet
#include "../network/lamp_protocol.hpp"
#include "../../version.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#endif

// Debug logging macros. The codebase convention is to gate every
// `Serial.*` call on LAMP_DEBUG so release builds (where Serial may
// never have been Serial.begin()'d) don't block on UART. Every
// diagnostic print in this file routes through these macros — they
// collapse to no-op on host builds (no Serial) and on release builds
// (no LAMP_DEBUG).
#if defined(LAMP_DEBUG) && (defined(ARDUINO) || defined(ESP_PLATFORM))
#define FWDIST_LOGF(...) Serial.printf(__VA_ARGS__)
#define FWDIST_LOGLN(s)  Serial.println(s)
#else
#define FWDIST_LOGF(...) ((void)0)
#define FWDIST_LOGLN(s)  ((void)0)
#endif

namespace lamp {

// Single global instance — the extern declaration lives in
// firmware_distributor.hpp so SocialBehavior (and any other consumer)
// can `#include "components/firmware/firmware_distributor.hpp"` and
// reference `lamp::firmwareDistributor` directly.
FirmwareDistributor firmwareDistributor;

namespace {

inline bool macsEqual(const uint8_t a[6], const uint8_t b[6]) {
  return std::memcmp(a, b, 6) == 0;
}

// Bind the OTA frame's footer-length field to the same constant the
// signature verifier uses, so a future LSIG footer resize can't leave
// distributor + receiver disagreeing on where the signed region ends.
constexpr uint16_t kFwFooterLenV1 =
    static_cast<uint16_t>(::lamp::firmware::kLsigFooterLen);

}  // namespace

// =============================================================================
// Lifecycle
// =============================================================================

void FirmwareDistributor::begin(FirmwareTransport* transport) {
  transport_ = transport;
  if (!transport_) {
    state_ = State::Disabled;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGLN("[fwdist] disabled (no transport bound)");
#endif
    return;
  }

  // Snapshot the lamp's MAC once — getMyMac is cheap but the streaming task
  // would otherwise hammer it per chunk.
  transport_->getMyMac(cachedSrcMac_);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  runningPartition_ = esp_ota_get_running_partition();
  if (!runningPartition_) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (no running partition)");
    return;
  }
  // Total length to distribute is the actual signed image size, NOT the
  // partition capacity. The running partition is sized in partitions.csv
  // to be larger than the image (~1.5 MB partition, ~1.35 MB signed
  // image), with the tail erased to 0xFF. Sending `partition->size`
  // bytes would stream 1.4 MB of mostly-erased flash garbage, and the
  // SHA wouldn't match what the receiver computes over its own newly-
  // written image.
  //
  // Discovery strategy: scan FORWARD for the lowest valid LSIG footer (the
  // running image's) and trust the signed length it reports. Forward, not
  // backward, is load-bearing — a smaller image re-flashed over a larger one
  // leaves a stale higher footer that a backward scan would grab first (see
  // discoverImageLength).
  if (!discoverImageLength(&firmwareTotalLen_)) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (could not discover image length)");
    return;
  }
  firmwareTotalChunks_ = static_cast<uint16_t>(
      (firmwareTotalLen_ + lamp_protocol::FW_CHUNK_SIZE - 1) /
      lamp_protocol::FW_CHUNK_SIZE);
  if (firmwareTotalLen_ <= kFwFooterLenV1 || firmwareTotalChunks_ == 0) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (running partition too small for OTA)");
    return;
  }

  // Pre-compute the sha256 prefix over the signed region (everything
  // before the 96-byte footer). Same scheme as the wisp's FirmwareCarrier
  // prep: stream the signed region through mbedtls SHA-256, take the
  // first 8 bytes of the digest. This is the image fingerprint we
  // advertise in OFFER + echo in DONE.
  if (!computeShaPrefixOnce(firmwareTotalLen_)) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (sha256 prefix compute failed)");
    return;
  }
#else
  // Native test path — leave the partition/sha state at construction
  // defaults; the test harness is expected to bypass begin() or stub
  // these fields directly.
#endif

  state_         = State::Idle;
  stateEnteredMs_ = 0;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  wakeSem_ = xSemaphoreCreateBinary();
  if (!wakeSem_) {
    FWDIST_LOGLN("[fwdist] failed to create wake semaphore");
    state_ = State::Disabled;
    return;
  }
  // Streaming task. Unpinned (xTaskCreate, not xTaskCreatePinnedToCore) —
  // the scheduler can place it on whichever core has slack. Priority 5
  // sits above the Arduino loop (1) but well below WiFi/IDF (18+).
  const BaseType_t ok = xTaskCreate(
      &FirmwareDistributor::streamingTaskTrampoline,
      "fwdist",
      kStreamingTaskStackSize,
      this,
      kStreamingTaskPriority,
      &streamingTask_);
  if (ok != pdPASS) {
    FWDIST_LOGLN("[fwdist] failed to create streaming task");
    state_ = State::Disabled;
    return;
  }
  FWDIST_LOGF("[fwdist] online; v=0x%08lx chunks=%u totalLen=%u\n",
                (unsigned long)lamp::FIRMWARE_VERSION,
                (unsigned)firmwareTotalChunks_,
                (unsigned)firmwareTotalLen_);
  // Stack high-water-mark for the loop task — this begin() runs from
  // setup(), and discoverImageLength + computeShaPrefixOnce both stack-
  // allocate 4 KB scratch buffers. The loop task's stack is 8 KB by
  // default; we want to confirm empirically that the 4 KB buffers fit
  // without painting near the redline. Reported in 32-bit words per the
  // FreeRTOS API; multiply by 4 for bytes-free.
  const UBaseType_t hwmWords = uxTaskGetStackHighWaterMark(nullptr);
  FWDIST_LOGF("[fwdist] loop-task stack HWM: %u words (%u bytes free)\n",
                (unsigned)hwmWords, (unsigned)(hwmWords * 4));
#endif
}

bool FirmwareDistributor::isInProgress() const {
  // Mid-flow = OfferSent / Streaming / Finalizing. Failed + Done are
  // tombstone states; the next tick reaps them back to Idle. We report
  // them as NOT in-progress so a follow-up considerPeerForOta for a
  // different peer can fire immediately after a Done/Failed tick boundary.
  return state_ == State::OfferSent || state_ == State::Streaming ||
         state_ == State::Finalizing;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

bool FirmwareDistributor::readPartitionBytes(uint32_t offset, size_t len,
                                             uint8_t* buf) const {
  if (!runningPartition_ || !buf) return false;
  if (offset + len > runningPartition_->size) return false;
  const esp_err_t err = esp_partition_read(runningPartition_, offset, buf, len);
  return err == ESP_OK;
}

bool FirmwareDistributor::discoverImageLength(uint32_t* outLen) const {
  if (!runningPartition_ || !outLen) return false;
  // Locate the LSIG footer of the image at the START of the partition — the
  // running image — by scanning FORWARD and returning the FIRST valid footer
  // (lowest offset). The signed length is whatever the footer says
  // (signedRegionLen == footerStart), not the last-non-0xFF byte.
  //
  // Forward, not backward, is load-bearing. esptool's app-only flash erases
  // only the bytes it writes, so a lamp re-flashed with an image SMALLER than
  // its predecessor keeps the OLD image's footer in the un-erased tail, at a
  // HIGHER offset than the current footer — and that stale footer is
  // internally self-consistent (it was a real image once), so the
  // signedRegionLen sanity check can't reject it. A backward scan finds the
  // stale footer first and streams the wrong image. The lowest valid footer
  // is always the running image's: a LARGER new image fully overwrites a
  // smaller predecessor (footer included), so no stale footer ever survives
  // BELOW the current one. (A normal mesh OTA erases a fixed sector span that
  // masks this, which is why it only bit esptool-flashed lamps.)
  //
  // 256-byte sliding window with a (kMagicLen - 1) overlap so a magic that
  // straddles a window boundary isn't missed. Called once at init, so the
  // forward read up to the footer is a one-time cost.
  constexpr size_t  kWindow   = 256;
  constexpr size_t  kMagicLen = 4;  // "LSIG"
  static const uint8_t kMagic[kMagicLen] = {'L', 'S', 'I', 'G'};
  uint8_t buf[kWindow + kMagicLen - 1];
  const uint32_t partSize = runningPartition_->size;
  uint32_t scanStart = 0;
  while (scanStart < partSize) {
    const size_t take = (partSize - scanStart > kWindow) ? kWindow
                                                         : (partSize - scanStart);
    // Read (kMagicLen - 1) past the window so a magic straddling the top
    // boundary is captured intact; those bytes belong to the next window.
    const size_t extra = (scanStart + take + kMagicLen - 1 <= partSize)
                            ? (kMagicLen - 1)
                            : 0;
    if (!readPartitionBytes(scanStart, take + extra, buf)) return false;
    for (size_t off = 0; off < take; ++off) {
      if (off + kMagicLen > take + extra) break;
      if (buf[off]     == kMagic[0] &&
          buf[off + 1] == kMagic[1] &&
          buf[off + 2] == kMagic[2] &&
          buf[off + 3] == kMagic[3]) {
        const uint32_t footerStart = scanStart + off;
        uint8_t lenBytes[4];
        if (!readPartitionBytes(footerStart + ::lamp::firmware::kLsigSignedLenOffset,
                                4, lenBytes)) return false;
        const uint32_t signedRegionLen =
            static_cast<uint32_t>(lenBytes[0]) |
            (static_cast<uint32_t>(lenBytes[1]) << 8) |
            (static_cast<uint32_t>(lenBytes[2]) << 16) |
            (static_cast<uint32_t>(lenBytes[3]) << 24);
        // Real footer sits immediately after its signed region. A
        // coincidental "LSIG" inside firmware data won't satisfy this.
        if (signedRegionLen == footerStart) {
          *outLen = footerStart + kFwFooterLenV1;
          return true;
        }
      }
    }
    scanStart += take;
  }
  return false;
}

bool FirmwareDistributor::computeShaPrefixOnce(uint32_t totalLen) {
  if (!runningPartition_) return false;
  if (totalLen <= kFwFooterLenV1) return false;
  const uint32_t signedLen = totalLen - kFwFooterLenV1;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  // mbedTLS API: 0 = SHA-256 (not SHA-224). Returns 0 on success.
  if (mbedtls_sha256_starts(&ctx, 0) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }

  // Stream the signed region through SHA-256 in 4 KB blocks. We can't
  // mbedtls_sha256_update from a memory-mapped flash region directly
  // because the partition is at a physical offset, not a CPU address.
  // 4 KB is sector-sized and a reasonable stack buffer.
  constexpr size_t kBlock = 4096;
  uint8_t buf[kBlock];
  uint32_t cursor = 0;
  while (cursor < signedLen) {
    const size_t want = (signedLen - cursor) > kBlock
                            ? kBlock
                            : (signedLen - cursor);
    if (!readPartitionBytes(cursor, want, buf)) {
      mbedtls_sha256_free(&ctx);
      return false;
    }
    if (mbedtls_sha256_update(&ctx, buf, want) != 0) {
      mbedtls_sha256_free(&ctx);
      return false;
    }
    cursor += want;
  }
  uint8_t digest[32];
  if (mbedtls_sha256_finish(&ctx, digest) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }
  mbedtls_sha256_free(&ctx);
  std::memcpy(sha256Prefix_, digest, 8);
  shaPrefixReady_ = true;
  FWDIST_LOGF("[fwdist] sha256 prefix = %02x%02x%02x%02x%02x%02x%02x%02x\n",
                sha256Prefix_[0], sha256Prefix_[1], sha256Prefix_[2],
                sha256Prefix_[3], sha256Prefix_[4], sha256Prefix_[5],
                sha256Prefix_[6], sha256Prefix_[7]);
  return true;
}

// Wake the streaming task. Safe from recv task + loop task + the task
// itself (idempotent give on a binary semaphore).
void FirmwareDistributor::wakeStreamingTask() {
  if (wakeSem_) xSemaphoreGive(wakeSem_);
}

void FirmwareDistributor::streamingTaskTrampoline(void* arg) {
  static_cast<FirmwareDistributor*>(arg)->streamingTaskLoop();
}

// Task body. Blocks on the wake semaphore until there's work, then drains
// streamingTaskStep() until the state machine transitions out of an active
// state. The semaphore is a token-bucket of one; we give from state-
// transition sites (initial OFFER, ACCEPT rx, REQ rewind) so the task
// can preempt the loop without waiting on the next idle poll boundary.
void FirmwareDistributor::streamingTaskLoop() {
  for (;;) {
    xSemaphoreTake(wakeSem_, pdMS_TO_TICKS(kStreamingIdlePollMs));
    while (streamingTaskStep(millis())) {
      // Cooperative yield between iterations. The send call is non-
      // blocking (queues to a shallow ring); a 10-tick delay gives the
      // WiFi task time to push frames over the air before we shove the
      // next one in. The 10ms cadence is the receiver-friendly value
      // tuned on hardware (see kStreamingChunkSpacingMs comment).
      vTaskDelay(pdMS_TO_TICKS(kStreamingChunkSpacingMs));
    }
  }
}

// One iteration of the streaming-task loop body. Returns true if the caller
// should loop again immediately, false to return to the wake semaphore.
bool FirmwareDistributor::streamingTaskStep(uint32_t nowMs) {
  State s;
  uint8_t  targetMacLocal[6];
  uint32_t lastOfferLocal = 0;
  uint8_t  offerRetriesLocal = 0;
  uint16_t nextChunkLocal = 0;
  uint16_t totalChunksLocal = 0;
  portENTER_CRITICAL(&stateMux_);
  s = state_;
  std::memcpy(targetMacLocal, targetMac_, 6);
  lastOfferLocal     = lastOfferSendMs_;
  offerRetriesLocal  = offerRetryCount_;
  nextChunkLocal     = nextChunkIdx_;
  totalChunksLocal   = totalChunks_;
  portEXIT_CRITICAL(&stateMux_);

  if (s == State::OfferSent) {
    if (offerRetriesLocal >= kMaxOfferRetries) {
      // No more retries — wait for the recv task (ACCEPT) or tick()
      // (kAcceptTimeoutMs) to break us out. Idle until then.
      return false;
    }
    const uint32_t sinceLast = nowMs - lastOfferLocal;
    if (sinceLast >= kOfferRetryIntervalMs) {
      // Bump retry count under mux BEFORE sending so a fast Accept-rx
      // interleave reads the updated value.
      portENTER_CRITICAL(&stateMux_);
      offerRetryCount_++;
      portEXIT_CRITICAL(&stateMux_);
      sendOfferFrame(targetMacLocal, nowMs, /*isRetry=*/true);
      return true;
    }
    const uint32_t waitMs = kOfferRetryIntervalMs - sinceLast;
    // xSemaphoreTake instead of vTaskDelay so an ACCEPT (or REQ, or any
    // state-pivoting recv event) wakes us immediately. Otherwise we'd
    // burn up to ~200 ms of dead air after ACCEPT before the next
    // iteration finds the state moved to Streaming. wakeStreamingTask
    // gives the semaphore from onAccept, onReq, onResult.
    xSemaphoreTake(wakeSem_, pdMS_TO_TICKS(waitMs));
    return true;
  }

  if (s == State::Streaming) {
    if (nextChunkLocal >= totalChunksLocal) {
      // Drained — transition to Finalizing and emit DONE. Take mux to
      // commit the state change; emitDone sends OUTSIDE the mux.
      bool needFinalize = false;
      portENTER_CRITICAL(&stateMux_);
      if (state_ == State::Streaming &&
          nextChunkIdx_ >= totalChunks_) {
        state_ = State::Finalizing;
        stateEnteredMs_ = nowMs;
        needFinalize = true;
      }
      portEXIT_CRITICAL(&stateMux_);
      if (needFinalize) emitDone(nowMs);
      // Streaming task has no more chunk work — let it idle. tick() owns
      // the FINALIZE timeout; the recv task wakes us on RESULT.
      return false;
    }
    const int rc = streamOneChunk(nowMs);
    if (rc == 1) {
      // NO_MEM — back off, then try same chunk next iteration.
      vTaskDelay(pdMS_TO_TICKS(kStreamingQueueBackoffMs));
      return true;
    }
    if (rc == 2) {
      // Partition read failure aborted the session inside streamOneChunk.
      return false;
    }
    return true;
  }

  return false;
}

// Single-chunk emit + index advance with mux discipline. Returns:
//   0 → chunk queued for send, indices advanced.
//   1 → ESP-NOW NO_MEM; caller should delay + retry.
//   2 → partition read failure; session aborted in-place.
int FirmwareDistributor::streamOneChunk(uint32_t nowMs) {
  // Snapshot the chunk index + target under mux. We must NOT advance
  // nextChunkIdx_ until the frame is queued; if a concurrent onReq
  // rewinds nextChunkIdx_ between our snapshot and the send, we re-read
  // on the next iteration.
  uint16_t chunkIdx;
  uint8_t  targetMacLocal[6];
  portENTER_CRITICAL(&stateMux_);
  if (state_ != State::Streaming) {
    portEXIT_CRITICAL(&stateMux_);
    return 0;
  }
  chunkIdx = nextChunkIdx_;
  std::memcpy(targetMacLocal, targetMac_, 6);
  portEXIT_CRITICAL(&stateMux_);

  if (!transport_ || !runningPartition_) return 2;

  uint8_t scratch[lamp_protocol::FW_CHUNK_SIZE];
  const uint32_t offset =
      static_cast<uint32_t>(chunkIdx) * lamp_protocol::FW_CHUNK_SIZE;
  // Last chunk is short when totalLen isn't a multiple of FW_CHUNK_SIZE.
  size_t want = lamp_protocol::FW_CHUNK_SIZE;
  if (offset >= firmwareTotalLen_) {
    // Defensive — should never happen (drain transition is guarded above).
    return 2;
  }
  if (offset + want > firmwareTotalLen_) {
    want = firmwareTotalLen_ - offset;
  }
  if (!readPartitionBytes(offset, want, scratch)) {
    FWDIST_LOGF("[fwdist] readPartitionBytes(off=%u len=%u) failed; aborting\n",
                  (unsigned)offset, (unsigned)want);
    portENTER_CRITICAL(&stateMux_);
    recordPeerFailure(nowMs);
    resetSession();
    state_ = State::Failed;
    stateEnteredMs_ = nowMs;
    portEXIT_CRITICAL(&stateMux_);
    return 2;
  }

  // seqCounter_ is touched by streamOneChunk + sendOfferFrame + emitDone;
  // all are called from non-recv contexts (streaming task / Core 1) but
  // bump under mux for safety.
  uint16_t seq;
  portENTER_CRITICAL(&stateMux_);
  seq = seqCounter_++;
  portEXIT_CRITICAL(&stateMux_);

  uint8_t buf[lamp_protocol::FW_CHUNK_MAX_SIZE];
  const size_t framed = lamp_protocol::buildFwChunk(
      buf, sizeof(buf), seq,
      cachedSrcMac_, targetMacLocal,
      chunkIdx, offset, scratch, static_cast<uint16_t>(want),
      targetProtocolVersion_);
  if (!framed) return 1;

  // Send OUTSIDE the mux. The transport's sendFrame queues to the ESP-NOW
  // TX ring (or BLE notify queue); a returned false maps to NO_MEM/queue-
  // full and the caller backs off.
  if (!transport_->sendFrame(buf, framed)) {
#ifdef LAMP_DEBUG
    // Rate-limit failure log so a sustained queue-full doesn't drown the UART
    // (at ~33 chunks/s the flood would obscure the rest of the OTA state).
    static uint32_t lastSendFailLogMs = 0;
    static uint32_t failCount = 0;
    failCount++;
    if (nowMs - lastSendFailLogMs > 1000) {
      FWDIST_LOGF("[fwdist] sendFrame fail chunkIdx=%u (n=%u in last sec)\n",
                  (unsigned)chunkIdx, (unsigned)failCount);
      lastSendFailLogMs = nowMs;
      failCount = 0;
    }
#endif
    return 1;
  }

  // Commit: advance nextChunkIdx_ IF it still points at the chunk we just
  // sent. If a concurrent onReq rewound it elsewhere, leave that value
  // alone — next iteration sends from there.
  uint16_t emittedCount = 0;
  bool     resumedToFwd  = false;
  uint16_t resumeToLog   = 0;
  portENTER_CRITICAL(&stateMux_);
  if (state_ == State::Streaming && nextChunkIdx_ == chunkIdx) {
    lastSentChunk_ = chunkIdx;
    nextChunkIdx_++;
    currentChunkRetries_ = 0;
    lastSentMs_ = nowMs;
    lastBurstSentChunks_++;
    // Smart-REQ resume: if we just finished serving the requested
    // chunks for a rewind, jump nextChunkIdx_ back to the forward-
    // progress position so we don't re-stream chunks the receiver
    // already has. See onReq for the comment that sets this up.
    if (resumeChunkIdx_ != 0 && nextChunkIdx_ >= reqEndIdx_ &&
        nextChunkIdx_ < resumeChunkIdx_) {
      resumeToLog       = resumeChunkIdx_;
      nextChunkIdx_     = resumeChunkIdx_;
      resumeChunkIdx_   = 0;
      reqEndIdx_        = 0;
      resumedToFwd      = true;
    }
    // Track monotonic high water for the indicator. Both the post-send
    // increment and the jump-forward above can be the new max; the
    // rewind paths (REQ + stall recovery) intentionally don't touch
    // this — the indicator should show forward progress only.
    sentProgress_.observe(nextChunkIdx_);
    if (lastBurstSentChunks_ >= kStreamProgressLogEvery) {
      emittedCount = nextChunkIdx_;
      lastBurstSentChunks_ = 0;
    }
  }
  portEXIT_CRITICAL(&stateMux_);
  if (resumedToFwd) {
    FWDIST_LOGF("[fwdist] REQ served; jumping forward to chunk %u\n",
                  (unsigned)resumeToLog);
  }
  if (emittedCount != 0) {
    FWDIST_LOGF("[fwdist] stream progress: sent %u/%u chunks\n",
                  (unsigned)emittedCount, (unsigned)totalChunks_);
  }
  return 0;
}

#endif  // ARDUINO || ESP_PLATFORM

// =============================================================================
// tick — drives ACCEPT/FINALIZE timeouts + stall watchdog + tombstone reaper
// =============================================================================

void FirmwareDistributor::tick(uint32_t nowMs) {
  if (state_ == State::Disabled) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Refresh nowMs from millis() — the caller passed a value captured
  // BEFORE potentially-slow loop work. Stale nowMs underflows uint32_t
  // subtractions and fires timeouts instantly.
  nowMs = millis();
#endif

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  State s;
  uint32_t stateEnteredMsLocal = 0;
  portENTER_CRITICAL(&stateMux_);
  s = state_;
  stateEnteredMsLocal = stateEnteredMs_;
  portEXIT_CRITICAL(&stateMux_);
#else
  State s = state_;
  uint32_t stateEnteredMsLocal = stateEnteredMs_;
#endif

  switch (s) {
    case State::Idle: {
      // No scan — Idle is entirely event-driven (considerPeerForOta).
      // But: if we entered Idle from a recent Done/Failed and still hold
      // OTA quiet mode (inter-session hold so the indicator stays solid
      // through the gap to the next OTA in the wave), expire the hold
      // when nothing new has armed us within kInterSessionQuietHoldMs.
      //
      // quietHoldUntilMs_ == 0 is the "no exit pending" sentinel set
      // at session start in emitOffer. We MUST guard the comparison
      // here — without the != 0 check, nowMs >= 0 is always true, and
      // any tick that observes Idle while a session is active (e.g.
      // a transient Done→Idle race or a streaming-task wake mid-tick)
      // would call exitQuiet prematurely. The compositor on the next
      // tick would then run the normal pipeline → IdleBehavior writes
      // defaultColors over the indicator → strip flickers between
      // progress and full base color.
      if (quietHeld_ && quietHoldUntilMs_ != 0 && nowMs >= quietHoldUntilMs_) {
        ::lamp::ota_quiet_mode::exitQuiet();
        quietHeld_         = false;
        quietHoldUntilMs_  = 0;
        lastSessionValid_  = false;
      }
      break;
    }

    case State::OfferSent: {
      // OFFER retry cadence is handled inside the streaming task. tick()
      // only watches the overall ACCEPT-timeout window.
      if (nowMs >= stateEnteredMsLocal &&
          (nowMs - stateEnteredMsLocal) > kAcceptTimeoutMs) {
        FWDIST_LOGLN("[fwdist] OFFER timeout; backing off peer");
#if defined(ARDUINO) || defined(ESP_PLATFORM)
        portENTER_CRITICAL(&stateMux_);
        recordPeerFailure(nowMs);
        resetSession();
        state_ = State::Failed;
        stateEnteredMs_ = nowMs;
        portEXIT_CRITICAL(&stateMux_);
#else
        recordPeerFailure(nowMs);
        resetSession();
        state_ = State::Failed;
        stateEnteredMs_ = nowMs;
#endif
      }
      break;
    }

    case State::Streaming: {
      // Streaming runs on the dedicated task. tick() only checks the
      // stall watchdog (no forward progress in kChunkResendMs → bump
      // retry counter; exceeded → fail the peer).
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      uint32_t lastSentMsLocal = 0;
      uint8_t  currentRetriesLocal = 0;
      uint16_t nextChunkLocal = 0;
      uint16_t totalChunksLocal = 0;
      uint16_t lastSentChunkLocal = 0;
      portENTER_CRITICAL(&stateMux_);
      lastSentMsLocal     = lastSentMs_;
      currentRetriesLocal = currentChunkRetries_;
      nextChunkLocal      = nextChunkIdx_;
      totalChunksLocal    = totalChunks_;
      lastSentChunkLocal  = lastSentChunk_;
      portEXIT_CRITICAL(&stateMux_);
#else
      uint32_t lastSentMsLocal     = lastSentMs_;
      uint8_t  currentRetriesLocal = currentChunkRetries_;
      uint16_t nextChunkLocal      = nextChunkIdx_;
      uint16_t totalChunksLocal    = totalChunks_;
      uint16_t lastSentChunkLocal  = lastSentChunk_;
#endif
      if (nextChunkLocal >= totalChunksLocal) break;
      if (nowMs >= lastSentMsLocal &&
          (nowMs - lastSentMsLocal) > kChunkResendMs) {
        if (currentRetriesLocal >= kRetriesPerChunk) {
          FWDIST_LOGLN("[fwdist] chunk retry budget exhausted; failing peer");
#if defined(ARDUINO) || defined(ESP_PLATFORM)
          portENTER_CRITICAL(&stateMux_);
          recordPeerFailure(nowMs);
          resetSession();
          state_ = State::Failed;
          stateEnteredMs_ = nowMs;
          portEXIT_CRITICAL(&stateMux_);
#else
          recordPeerFailure(nowMs);
          resetSession();
          state_ = State::Failed;
          stateEnteredMs_ = nowMs;
#endif
          break;
        }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
        portENTER_CRITICAL(&stateMux_);
        currentChunkRetries_++;
        nextChunkIdx_ = lastSentChunkLocal;  // rewind to stalled chunk
        portEXIT_CRITICAL(&stateMux_);
        wakeStreamingTask();
#else
        currentChunkRetries_++;
        nextChunkIdx_ = lastSentChunkLocal;
#endif
      }
      break;
    }

    case State::Finalizing: {
      if (nowMs >= stateEnteredMsLocal &&
          (nowMs - stateEnteredMsLocal) > kFinalizeTimeoutMs) {
        FWDIST_LOGLN("[fwdist] FINALIZE timeout; short-backing off peer");
#if defined(ARDUINO) || defined(ESP_PLATFORM)
        portENTER_CRITICAL(&stateMux_);
        recordPeerFailureFinalize(nowMs);
        resetSession();
        state_ = State::Failed;
        stateEnteredMs_ = nowMs;
        portEXIT_CRITICAL(&stateMux_);
#else
        recordPeerFailureFinalize(nowMs);
        resetSession();
        state_ = State::Failed;
        stateEnteredMs_ = nowMs;
#endif
      }
      break;
    }

    case State::Failed:
    case State::Done:
      // Tombstone: return to Idle on the next tick. No scan/timing reset
      // bookkeeping is needed since Idle is event-driven now.
      //
      // Capture last-session identity BEFORE resetSession() blanks the
      // targetMac/totalChunks fields. ota_indicator reads this through
      // getLastSession() so the strip keeps painting a held "completed
      // bar" for this peer during the inter-session quiet hold rather
      // than flashing back to normal animation between OTAs in a wave.
      std::memcpy(lastSessionPeerMac_, targetMac_, 6);
      lastSessionTotalChunks_ = totalChunks_;
      lastSessionValid_       = true;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
      portENTER_CRITICAL(&stateMux_);
      resetSession();
      state_ = State::Idle;
      stateEnteredMs_ = nowMs;
      portEXIT_CRITICAL(&stateMux_);
#else
      resetSession();
      state_ = State::Idle;
      stateEnteredMs_ = nowMs;
#endif
      // Defer exitQuiet — keep the strip showing the OTA indicator
      // through the gap to the next session. The Idle case expires the
      // hold if no new session arms us within kInterSessionQuietHoldMs.
      // If a new emitOffer fires inside the window, it inherits the
      // already-held quiet (no enterQuiet call needed) and clears the
      // pending exit, so OTAs in a wave look continuous on the strip.
      quietHoldUntilMs_ = nowMs + kInterSessionQuietHoldMs;
      break;

    case State::Disabled:
      break;
  }
}

// =============================================================================
// Peer trigger (event-driven, replaces wisp's scan loop)
// =============================================================================

void FirmwareDistributor::considerPeerForOta(const uint8_t peerMac[6],
                                             uint32_t peerVersion,
                                             uint8_t peerProtocolVersion,
                                             uint32_t nowMs,
                                             const char* peerFwChannel) {
  if (state_ != State::Idle) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "state=%u (not Idle)\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion, (unsigned)state_);
    return;
  }
  if (!transport_) return;
  // Peer protocol version is zero until we've heard a HELLO from them
  // — skip the consider entirely so we never emit OFFERs at an unknown
  // version. Once we hear a HELLO this gate opens.
  if (peerProtocolVersion < lamp_protocol::PROTOCOL_VERSION_RX_MIN ||
      peerProtocolVersion > lamp_protocol::PROTOCOL_VERSION_RX_MAX) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "peer protocol=0x%02X outside [0x%02X, 0x%02X]\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion, (unsigned)peerProtocolVersion,
                (unsigned)lamp_protocol::PROTOCOL_VERSION_RX_MIN,
                (unsigned)lamp_protocol::PROTOCOL_VERSION_RX_MAX);
    return;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (!runningPartition_ || !shaPrefixReady_) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "partition=%d shaReady=%d\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion,
                runningPartition_ != nullptr, (int)shaPrefixReady_);
    return;
  }
#endif
  if (peerVersion >= lamp::FIRMWARE_VERSION) return;
  // Type/channel gate: when we know the peer's {type}-{channel} (from its
  // HELLO_TLV_FW_CHANNEL) and it differs from ours, don't OFFER — a snafu
  // lamp can't run a standard image and channels must not cross. An unknown
  // (empty) channel means an older peer that doesn't emit the TLV; fall
  // through and let the receiver's onOfferOnLoop silent-drop be the backstop.
  if (peerFwChannel && peerFwChannel[0] != '\0' &&
      std::strncmp(peerFwChannel, lamp::FIRMWARE_CHANNEL_STR,
                   lamp_protocol::FW_CHANNEL_LEN) != 0) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X skip: "
                "peer channel=%s != ours=%s\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                peerFwChannel, lamp::FIRMWARE_CHANNEL_STR);
    return;
  }
  if (peerIsInBackoff(peerMac, nowMs)) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "peer in backoff\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion);
    return;
  }
  FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X "
              "our=0x%08X → OFFER\n",
              peerMac[0], peerMac[1], peerMac[2],
              peerMac[3], peerMac[4], peerMac[5],
              (unsigned)peerVersion, (unsigned)lamp::FIRMWARE_VERSION);
  // Known cross-type/channel peers were filtered above. An unknown-channel
  // peer (older firmware, no TLV) can still reach here; its own onOfferOnLoop
  // silent-drop (firmware_receiver.cpp::channelMatchesOurs) is the backstop —
  // the OFFER times out and it lands in our 10-minute backoff ring.
  targetProtocolVersion_ = peerProtocolVersion;
  emitOffer(peerMac, peerVersion, nowMs);
}

// =============================================================================
// Session bookkeeping
// =============================================================================

void FirmwareDistributor::resetSession() {
  std::memset(targetMac_, 0, 6);
  targetProtocolVersion_ = 0;
  totalChunks_     = 0;
  nextChunkIdx_    = 0;
  sentProgress_.reset();
  lastSentChunk_   = 0;
  lastSentMs_      = 0;
  currentChunkRetries_ = 0;
  sessionOfferSeq_ = 0;
  sessionVersion_  = 0;
  sessionTotalLen_ = 0;
  lastOfferSendMs_ = 0;
  offerRetryCount_ = 0;
  lastBurstSentChunks_ = 0;
  reqCountThisSession_ = 0;
  resumeChunkIdx_  = 0;
  reqEndIdx_       = 0;
}

bool FirmwareDistributor::getLastSession(uint8_t outMac[6],
                                        uint16_t& outTotalChunks) const {
  // ota_indicator probes this during the inter-session quiet hold so
  // the strip can render a held "we just finished" full bar instead
  // of falling back to the dim-background no-session branch.
  if (!lastSessionValid_) return false;
  std::memcpy(outMac, lastSessionPeerMac_, 6);
  outTotalChunks = lastSessionTotalChunks_;
  return true;
}

bool FirmwareDistributor::peerIsInBackoff(const uint8_t mac[6],
                                          uint32_t nowMs) const {
  for (size_t i = 0; i < kPenaltyRingSize; ++i) {
    const auto& p = penalties_[i];
    if (!p.used) continue;
    if (!macsEqual(p.mac, mac)) continue;
    if (nowMs < p.backoffUntilMs) return true;
  }
  return false;
}

void FirmwareDistributor::notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                                          uint32_t durationMs) {
  for (size_t i = 0; i < kPenaltyRingSize; ++i) {
    if (penalties_[i].used && macsEqual(penalties_[i].mac, mac)) {
      penalties_[i].backoffUntilMs = nowMs + durationMs;
      return;
    }
  }
  penalties_[penaltyHead_].used = true;
  std::memcpy(penalties_[penaltyHead_].mac, mac, 6);
  penalties_[penaltyHead_].backoffUntilMs = nowMs + durationMs;
  penaltyHead_ = (penaltyHead_ + 1) % kPenaltyRingSize;
}

void FirmwareDistributor::recordPeerFailure(uint32_t nowMs) {
  bool nonzero = false;
  for (int i = 0; i < 6; ++i) {
    if (targetMac_[i] != 0) { nonzero = true; break; }
  }
  if (nonzero) notePeerBackoff(targetMac_, nowMs, kPeerBackoffMs);
}

void FirmwareDistributor::recordPeerFailureFinalize(uint32_t nowMs) {
  bool nonzero = false;
  for (int i = 0; i < 6; ++i) {
    if (targetMac_[i] != 0) { nonzero = true; break; }
  }
  if (nonzero) notePeerBackoff(targetMac_, nowMs, kPeerFinalizeBackoffMs);
}

// =============================================================================
// OFFER + DONE emit
// =============================================================================

void FirmwareDistributor::emitOffer(const uint8_t targetMac[6],
                                    uint32_t peerVersion, uint32_t nowMs) {
  if (!transport_) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (!runningPartition_ || !shaPrefixReady_) return;
#endif
  if (firmwareTotalLen_ == 0 || firmwareTotalChunks_ == 0) return;
  (void)peerVersion;

  // Init session state under the mux so the recv task + streaming task see
  // a consistent snapshot.
  //
  // Re-check `state_ == Idle` INSIDE
  // the mux to close the considerPeerForOta TOCTOU race. Without this,
  // two consecutive ticks could each see Idle outside the mux, both
  // proceed to emitOffer, and the second would overwrite the first's
  // session state mid-flow.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  if (state_ != State::Idle) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
#endif
    return;
  }
  std::memcpy(targetMac_, targetMac, 6);
  sessionOfferSeq_  = seqCounter_++;
  totalChunks_      = firmwareTotalChunks_;
  nextChunkIdx_     = 0;
  lastSentChunk_    = 0;
  lastSentMs_       = 0;
  currentChunkRetries_ = 0;
  sessionVersion_   = lamp::FIRMWARE_VERSION;
  sessionTotalLen_  = firmwareTotalLen_;
  offerRetryCount_  = 0;
  lastOfferSendMs_  = nowMs;
  lastBurstSentChunks_ = 0;
  reqCountThisSession_ = 0;
  // Tentative OfferSent BEFORE the send so a near-instant ACCEPT arriving
  // on the recv task pre-transition is correctly matched. If the send
  // fails we roll back below.
  state_ = State::OfferSent;
  stateEnteredMs_ = nowMs;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif

  // Enter OTA quiet mode for the duration of the session. Distributor
  // side is always EspNow (no BLE-initiated outbound OTA) so we
  // unconditionally tear down the GATT connection + pause adv/scan +
  // suspend behaviors. Exited on the unified Idle/Failed tombstone
  // below, with an inter-session hold so OTA waves don't flicker the
  // strip between sessions. quietHeld_ tracks our share of the
  // refcount: if the prior session's exit was deferred, just inherit
  // the hold and clear the pending expiry — we never went un-quiet,
  // so we don't bump the count again.
  if (!quietHeld_) {
    ::lamp::ota_quiet_mode::enterQuiet(/*tearDownRadio=*/true);
    quietHeld_ = true;
  }
  quietHoldUntilMs_ = 0;
  lastSessionValid_ = false;  // new session takes over the indicator

  if (!sendOfferFrame(targetMac, nowMs, /*isRetry=*/false)) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portENTER_CRITICAL(&stateMux_);
    resetSession();
    state_ = State::Idle;
    portEXIT_CRITICAL(&stateMux_);
#else
    resetSession();
    state_ = State::Idle;
#endif
    // OFFER frame send failed — we never got the session off the
    // ground, so don't bother holding quiet for the next attempt
    // (probably a transport problem, not a peer-cadence one). Drop
    // straight back to normal animations.
    ::lamp::ota_quiet_mode::exitQuiet();
    quietHeld_ = false;
    quietHoldUntilMs_ = 0;
    return;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Kick the streaming task — it owns OFFER retries from here on.
  wakeStreamingTask();
#endif
}

bool FirmwareDistributor::sendOfferFrame(const uint8_t targetMac[6],
                                         uint32_t nowMs, bool isRetry) {
  if (!transport_) return false;

  uint16_t sessionOfferSeqLocal;
  uint32_t sessionVersionLocal;
  uint32_t sessionTotalLenLocal;
  uint16_t totalChunksLocal;
  uint8_t  sha[8];
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  sessionOfferSeqLocal = sessionOfferSeq_;
  sessionVersionLocal  = sessionVersion_;
  sessionTotalLenLocal = sessionTotalLen_;
  totalChunksLocal     = totalChunks_;
  std::memcpy(sha, sha256Prefix_, 8);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif

  uint8_t buf[lamp_protocol::FW_OFFER_FIXED_SIZE];
  const char* channel = lamp::FIRMWARE_CHANNEL_STR;
  const size_t channelLen = channel ? std::strlen(channel) : 0;
  const size_t n = lamp_protocol::buildFwOffer(
      buf, sizeof(buf), sessionOfferSeqLocal,
      cachedSrcMac_, targetMac,
      sessionVersionLocal, sessionTotalLenLocal, lamp_protocol::FW_CHUNK_SIZE,
      channel, channelLen,
      sha, kFwFooterLenV1, totalChunksLocal,
      targetProtocolVersion_);
  if (!n) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGLN("[fwdist] buildFwOffer failed (defensive)");
#endif
    return false;
  }
  const bool sent = transport_->sendFrame(buf, n);
  // Bump lastOfferSendMs_ regardless
  // of send success. Without this, a transient transport NO_MEM failure
  // would leave lastOfferSendMs_ unchanged, so the next streaming task
  // tick would see `sinceLast >= kOfferRetryIntervalMs` immediately and
  // burn the retry budget in a tight loop. Apply the cooldown either way.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  lastOfferSendMs_ = nowMs;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif
  if (!sent) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGLN("[fwdist] OFFER send failed (transport returned false)");
#endif
    return false;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  FWDIST_LOGF("[fwdist] OFFER%s -> %02X:%02X:%02X:%02X:%02X:%02X "
                "v=0x%08lx chunks=%u seq=%u\n",
                isRetry ? " (retry)" : "",
                targetMac[0], targetMac[1], targetMac[2],
                targetMac[3], targetMac[4], targetMac[5],
                (unsigned long)sessionVersionLocal, (unsigned)totalChunksLocal,
                (unsigned)sessionOfferSeqLocal);
#else
  (void)isRetry;
#endif
  return true;
}

void FirmwareDistributor::emitDone(uint32_t nowMs) {
  if (!transport_) return;
  // Snapshot session fields under mux. seq is captured ONCE here and reused
  // across every retry attempt below; receiver's onDoneOnLoop is state-
  // guarded so a duplicate DONE arriving post-verify is silently dropped.
  uint32_t sessionVersionLocal;
  uint32_t sessionTotalLenLocal;
  uint8_t  sha[8];
  uint8_t  targetMacLocal[6];
  uint16_t seq;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  sessionVersionLocal  = sessionVersion_;
  sessionTotalLenLocal = sessionTotalLen_;
  std::memcpy(sha, sha256Prefix_, 8);
  std::memcpy(targetMacLocal, targetMac_, 6);
  seq = seqCounter_++;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif

  uint8_t buf[lamp_protocol::FW_DONE_FIXED_SIZE];
  const size_t n = lamp_protocol::buildFwDone(
      buf, sizeof(buf), seq,
      cachedSrcMac_, targetMacLocal,
      sessionVersionLocal, sessionTotalLenLocal,
      sha, kFwFooterLenV1, targetProtocolVersion_);
  if (!n) return;

  // First attempt + up to kMaxDoneRetries follow-ups. Bail early if the
  // state pivots out of Finalizing — onResult (Done/Failed), onReq (back
  // to Streaming for late gap fill), or the tick() finalize-timeout path
  // can all change state under us.
  const uint8_t budget = 1 + kMaxDoneRetries;
  for (uint8_t attempt = 0; attempt < budget; ++attempt) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    State sNow;
    portENTER_CRITICAL(&stateMux_);
    sNow = state_;
    portEXIT_CRITICAL(&stateMux_);
    if (sNow != State::Finalizing) {
      FWDIST_LOGF("[fwdist] DONE retry bail: state pivoted (attempts=%u)\n",
                    (unsigned)attempt);
      return;
    }
#else
    if (state_ != State::Finalizing) return;
#endif
    transport_->sendFrame(buf, n);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGF("[fwdist] DONE%s -> %02X:%02X:%02X:%02X:%02X:%02X "
                  "(attempt %u/%u, waiting RESULT)\n",
                  attempt == 0 ? "" : " (retry)",
                  targetMacLocal[0], targetMacLocal[1], targetMacLocal[2],
                  targetMacLocal[3], targetMacLocal[4], targetMacLocal[5],
                  (unsigned)(attempt + 1), (unsigned)budget);
    if (attempt + 1 < budget) {
      // Yield the CPU so the WiFi task can deliver the queued frame, the
      // peer can process it, and a RESULT can arrive and pivot state_ →
      // Done/Failed before our next loop iteration. xSemaphoreTake (vs
      // vTaskDelay) so onResult's wakeStreamingTask preempts our wait
      // — otherwise we'd send up to 3 more DONE frames after RESULT
      // already landed, just because we were mid-sleep when it arrived.
      xSemaphoreTake(wakeSem_, pdMS_TO_TICKS(kDoneRetryIntervalMs));
    }
#endif
  }
  (void)nowMs;
}

// =============================================================================
// Inbound dispatchers — Core 0 (WiFi recv task)
// =============================================================================

void FirmwareDistributor::onAcceptOnRecvTask(
    const lamp_protocol::ParsedFwAccept& a) {
  if (state_ == State::Disabled) return;
  if (!transport_) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  const uint32_t nowMs = millis();
#else
  const uint32_t nowMs = 0;
#endif
  onAccept(a, nowMs);
}

void FirmwareDistributor::onReqOnRecvTask(const lamp_protocol::ParsedFwReq& r) {
  if (state_ == State::Disabled) return;
  if (!transport_) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  const uint32_t nowMs = millis();
#else
  const uint32_t nowMs = 0;
#endif
  onReq(r, nowMs);
}

void FirmwareDistributor::onResultOnRecvTask(
    const lamp_protocol::ParsedFwResult& r) {
  if (state_ == State::Disabled) return;
  if (!transport_) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  const uint32_t nowMs = millis();
#else
  const uint32_t nowMs = 0;
#endif
  onResult(r, nowMs);
}

void FirmwareDistributor::onAccept(const lamp_protocol::ParsedFwAccept& a,
                                   uint32_t nowMs) {
  bool wake = false;
  bool logAccept = false;
  bool logBackoff = false;
  uint8_t logStatus = 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  if (state_ != State::OfferSent ||
      !macsEqual(a.sourceMac, targetMac_) ||
      a.offerSeq != sessionOfferSeq_ ||
      a.version != sessionVersion_) {
    // Snapshot reasons under the mux so the post-exit log is consistent.
    const uint8_t  logState        = static_cast<uint8_t>(state_);
    const bool     logMacMismatch  = !macsEqual(a.sourceMac, targetMac_);
    const uint16_t logRxSeq        = a.offerSeq;
    const uint16_t logExpectedSeq  = sessionOfferSeq_;
    const uint32_t logRxVersion    = a.version;
    const uint32_t logExpectedVer  = sessionVersion_;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
    // Only log when state is OfferSent — that's when we're actually
    // expecting an ACCEPT. Drops in Idle/Failed/etc are routine.
    if (logState == static_cast<uint8_t>(State::OfferSent)) {
      FWDIST_LOGF("[fwdist] ACCEPT drop: state=%u macMismatch=%d "
                  "seq(rx=%u expect=%u) version(rx=0x%08X expect=0x%08X)\n",
                  (unsigned)logState, (int)logMacMismatch,
                  (unsigned)logRxSeq, (unsigned)logExpectedSeq,
                  (unsigned)logRxVersion, (unsigned)logExpectedVer);
    }
#else
    (void)logState; (void)logMacMismatch; (void)logRxSeq;
    (void)logExpectedSeq; (void)logRxVersion; (void)logExpectedVer;
#endif
    return;
  }
  if (a.status != lamp_protocol::FwAcceptStatus::Accept) {
    logBackoff = true;
    logStatus = static_cast<uint8_t>(a.status);
    recordPeerFailure(nowMs);
    resetSession();
    state_ = State::Failed;
    stateEnteredMs_ = nowMs;
  } else {
    state_ = State::Streaming;
    stateEnteredMs_ = nowMs;
    // Stamp lastSentMs_ to now so the tick() stall watchdog gives the
    // streaming task a fair kChunkResendMs window to actually send the
    // first chunk before bumping the retry counter. resetSession() set
    // lastSentMs_ to 0; without this re-stamp, tick() sees
    // (nowMs - 0) > kChunkResendMs as true immediately after ACCEPT
    // and exhausts the retry budget within milliseconds (4 increments
    // fire in tight tick iterations before the streaming task on its
    // own FreeRTOS task has had a chance to call sendFrame even once).
    // Bites hard under BLE coex pressure when sendFrame transiently
    // fails.
    lastSentMs_ = nowMs;
    wake = true;
    logAccept = true;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);

  if (logBackoff) {
    FWDIST_LOGF("[fwdist] ACCEPT status=%u; backing off\n",
                  (unsigned)logStatus);
  }
  if (logAccept) {
    FWDIST_LOGF("[fwdist] ACCEPT from %02X:%02X:%02X:%02X:%02X:%02X; streaming\n",
                  a.sourceMac[0], a.sourceMac[1], a.sourceMac[2],
                  a.sourceMac[3], a.sourceMac[4], a.sourceMac[5]);
  }
  if (wake) wakeStreamingTask();
#else
  (void)wake;
  (void)logAccept;
  (void)logBackoff;
  (void)logStatus;
#endif
}

void FirmwareDistributor::onReq(const lamp_protocol::ParsedFwReq& r,
                                uint32_t nowMs) {
  bool wake = false;
  bool log = false;
  bool abort = false;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  if ((state_ != State::Streaming && state_ != State::Finalizing) ||
      !macsEqual(r.sourceMac, targetMac_) ||
      r.firstChunkIdx >= totalChunks_) {
    // Snapshot reasons under the mux so the post-exit log is consistent.
    const uint8_t  logState         = static_cast<uint8_t>(state_);
    const bool     logMacMismatch   = !macsEqual(r.sourceMac, targetMac_);
    const bool     logIdxOob        = r.firstChunkIdx >= totalChunks_;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
    FWDIST_LOGF("[fwdist] REQ drop from %02X:%02X:%02X:%02X:%02X:%02X first=%u "
                "state=%u macMismatch=%d idxOob=%d\n",
                r.sourceMac[0], r.sourceMac[1], r.sourceMac[2],
                r.sourceMac[3], r.sourceMac[4], r.sourceMac[5],
                (unsigned)r.firstChunkIdx, (unsigned)logState,
                (int)logMacMismatch, (int)logIdxOob);
#else
    (void)logState; (void)logMacMismatch; (void)logIdxOob;
#endif
    return;
  }
  // Per-session REQ budget. A receiver that REQs many times in one
  // session is either suffering catastrophic packet loss (in which case
  // restarting the session via peer backoff is correct) or attempting to
  // pin the sender in a rewind loop (malicious / buggy). Either way:
  // record peer failure (10-minute backoff) and tombstone the session.
  // Order matters: recordPeerFailure reads targetMac_, so it MUST run
  // before resetSession() clears it.
  if (reqCountThisSession_ >= kMaxReqPerSession) {
    recordPeerFailure(nowMs);
    resetSession();
    state_         = State::Failed;
    stateEnteredMs_ = nowMs;
    abort          = true;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
    FWDIST_LOGF("[fwdist] REQ flood: %u in this session, aborting\n",
                (unsigned)kMaxReqPerSession);
    (void)wake;
    (void)log;
    (void)abort;
#endif
    return;
  }
  ++reqCountThisSession_;
  // Smart rewind: save the forward-progress position in resumeChunkIdx_
  // so the streaming task can JUMP BACK to forward emit after it sends
  // the requested chunk(s). Without this, naive rewind re-streams every
  // chunk from `firstChunkIdx` to `totalChunks_`, most of which the
  // receiver already has — wasted RF time + BLE coex pressure that turns
  // a 1-minute OTA into 5+ minutes. We only save resumeChunkIdx_ on the
  // FIRST rewind after a forward-progress run; subsequent REQs during
  // an in-flight rewind don't reset the saved forward cursor but DO
  // extend the rewind window's end-index so the new REQ's range is
  // covered before the jump-forward fires.
  const uint16_t newReqEnd = r.firstChunkIdx + r.chunkCount;
  if (resumeChunkIdx_ == 0 &&
      nextChunkIdx_ > newReqEnd) {
    resumeChunkIdx_ = nextChunkIdx_;
    reqEndIdx_      = newReqEnd;
  } else if (resumeChunkIdx_ != 0 && newReqEnd > reqEndIdx_) {
    // Stacked REQ during an in-flight rewind. Extend the window's end
    // so the jump-forward in streamOneChunk doesn't fire until the new
    // REQ's tail has been served. Without this, a stacked REQ
    // overwrites nextChunkIdx_ but the OLD reqEndIdx_ still gates the
    // forward jump — chunks between the two ranges get re-REQ'd by
    // the receiver's stall watchdog instead of served in this round.
    reqEndIdx_ = newReqEnd;
  }
  nextChunkIdx_        = r.firstChunkIdx;
  currentChunkRetries_ = 0;
  lastSentMs_          = nowMs;
  if (state_ == State::Finalizing) {
    state_ = State::Streaming;
    stateEnteredMs_ = nowMs;
  }
  wake = true;
  log  = true;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);

  if (log) {
    FWDIST_LOGF("[fwdist] REQ first=%u count=%u reason=%u; rewinding (%u/%u)\n",
                  (unsigned)r.firstChunkIdx, (unsigned)r.chunkCount,
                  (unsigned)static_cast<uint8_t>(r.reason),
                  (unsigned)reqCountThisSession_,
                  (unsigned)kMaxReqPerSession);
  }
  if (wake) wakeStreamingTask();
#else
  (void)wake;
  (void)log;
  (void)abort;
#endif
}

void FirmwareDistributor::onResult(const lamp_protocol::ParsedFwResult& r,
                                   uint32_t nowMs) {
  bool logSuccess = false;
  bool logFailure = false;
  uint8_t logStatus = 0;
  uint8_t logDetail = 0;
  uint8_t logMac[6] = {0};
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  if (state_ != State::Finalizing ||
      !macsEqual(r.sourceMac, targetMac_) ||
      r.version != sessionVersion_) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
#endif
    return;
  }
  const uint8_t status = static_cast<uint8_t>(r.status);
  std::memcpy(logMac, r.sourceMac, 6);
  if (status == static_cast<uint8_t>(lamp_protocol::FwResultStatus::Success)) {
    resetSession();
    state_ = State::Done;
    stateEnteredMs_ = nowMs;
    logSuccess = true;
  } else {
    logFailure = true;
    logStatus = status;
    logDetail = r.detail;
    recordPeerFailure(nowMs);
    resetSession();
    state_ = State::Failed;
    stateEnteredMs_ = nowMs;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);

  if (logSuccess) {
    FWDIST_LOGF("[fwdist] RESULT success from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  logMac[0], logMac[1], logMac[2],
                  logMac[3], logMac[4], logMac[5]);
  }
  if (logFailure) {
    FWDIST_LOGF("[fwdist] RESULT failure status=%u detail=%u\n",
                  (unsigned)logStatus, (unsigned)logDetail);
  }
#else
  (void)logSuccess;
  (void)logFailure;
  (void)logStatus;
  (void)logDetail;
  (void)logMac;
#endif
}

}  // namespace lamp
