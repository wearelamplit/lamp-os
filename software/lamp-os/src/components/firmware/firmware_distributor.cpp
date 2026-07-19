#include "firmware_distributor.hpp"

#include <cstring>

#include "firmware_receiver.hpp"  // FirmwareTransport interface
#include "firmware_signature.hpp"  // kLsigFooterLen
#include "ota_channel.hpp"
#include "components/network/ble/ble_control.hpp"  // pauseRadioForOta / resumeRadioAfterOta
#include "components/firmware/ota_quiet_mode.hpp"     // enterQuiet / exitQuiet
#include "components/network/protocol/lamp_protocol.hpp"
#include "../../version.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#endif

#if defined(LAMP_DEBUG) && (defined(ARDUINO) || defined(ESP_PLATFORM))
#define FWDIST_LOGF(...) Serial.printf(__VA_ARGS__)
#define FWDIST_LOGLN(s)  Serial.println(s)
#else
#define FWDIST_LOGF(...) ((void)0)
#define FWDIST_LOGLN(s)  ((void)0)
#endif

namespace lamp {

FirmwareDistributor firmwareDistributor;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
FirmwareDistributor* FirmwareDistributor::s_streamers[2] = {nullptr, nullptr};
uint8_t              FirmwareDistributor::s_streamerCount = 0;
SemaphoreHandle_t    FirmwareDistributor::s_sharedWake = nullptr;
TaskHandle_t         FirmwareDistributor::s_sharedTask = nullptr;
#endif

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

void FirmwareDistributor::begin(FirmwareTransport* transport) {
  transport_ = transport;
  if (!transport_) {
    state_ = State::Disabled;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGLN("[fwdist] disabled (no transport bound)");
#endif
    return;
  }

  // Dev channel is USB-only; never source OTA even if a footer somehow exists.
  if (std::strstr(lamp::FIRMWARE_CHANNEL_STR, "-dev")) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (dev channel)");
    return;
  }

  // Snapshot the lamp's MAC once. getMyMac is cheap but the streaming task
  // would otherwise hammer it per chunk.
  transport_->getMyMac(cachedSrcMac_);

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (fsHooks_) {
    // FS: stream the spiffs partition with a fixed length + manifest digest
    // prefix. Digest comes from fw.lsig via hook because a raw-partition SHA
    // differs per lamp.
    runningPartition_ =
        static_cast<const esp_partition_t*>(fsHooks_->partition());
    if (!runningPartition_) {
      state_ = State::Disabled;
      FWDIST_LOGLN("[fsdist] disabled (no spiffs partition)");
      return;
    }
    if (!fsHooks_->lengthAndDigest(&firmwareTotalLen_, sha256Prefix_,
                                   sha256Full_, imageSignature_)) {
      state_ = State::Disabled;
      FWDIST_LOGLN("[fsdist] disabled (length/digest)");
      return;
    }
    shaPrefixReady_ = true;
    // FS offers now carry the same digest + signature auth trailer as firmware,
    // so the receiver verifies before it unmounts + erases the live web UI.
    authReady_ = true;
    firmwareTotalChunks_ = static_cast<uint16_t>(
        (firmwareTotalLen_ + sessionChunkSize_ - 1) / sessionChunkSize_);
    if (firmwareTotalChunks_ == 0) {
      state_ = State::Disabled;
      FWDIST_LOGLN("[fsdist] disabled (empty fs image)");
      return;
    }
  } else {
  runningPartition_ = esp_ota_get_running_partition();
  if (!runningPartition_) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (no running partition)");
    return;
  }
  // Distribute the actual signed image size, not the partition capacity (the
  // partition is larger, tail erased to 0xFF; sending it would stream garbage
  // and break the receiver's SHA). Scan forward for the lowest valid LSIG
  // footer (see discoverImageLength for why a backward scan can find a stale
  // footer from a smaller predecessor image).
  if (!discoverImageLength(&firmwareTotalLen_)) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (could not discover image length)");
    return;
  }
  firmwareTotalChunks_ = static_cast<uint16_t>(
      (firmwareTotalLen_ + sessionChunkSize_ - 1) / sessionChunkSize_);
  if (firmwareTotalLen_ <= kFwFooterLenV1 || firmwareTotalChunks_ == 0) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (running partition too small for OTA)");
    return;
  }

  if (!computeShaPrefixOnce(firmwareTotalLen_)) {
    state_ = State::Disabled;
    FWDIST_LOGLN("[fwdist] disabled (sha256 prefix compute failed)");
    return;
  }
  // Read the running image's ed25519 signature from its LSIG footer and verify
  // it against the running digest + kFirmwarePubkey. A running image that doesn't
  // self-verify (unsigned, foreign key, corrupt footer) must never OFFER: the
  // receiver would stream the whole image and reject it at end-verify, looping.
  {
    const uint32_t footerStart = firmwareTotalLen_ - kFwFooterLenV1;
    if (!readPartitionBytes(
            footerStart + ::lamp::firmware::kLsigSignatureOffset,
            ::lamp::firmware::kLsigSignatureLen, imageSignature_)) {
      state_ = State::Disabled;
      FWDIST_LOGLN("[fwdist] disabled (signature read failed)");
      return;
    }
    if (!::lamp::firmware::verifyFirmwareDigestSignature(sha256Full_,
                                                         imageSignature_)) {
      state_ = State::Disabled;
      FWDIST_LOGLN("[fwdist] disabled (running image signature invalid)");
      return;
    }
    authReady_ = true;
  }
  }  // else: firmware distribution
#else
  // Native test path: tests bypass begin() or stub these fields directly.
#endif

  state_         = State::Idle;
  stateEnteredMs_ = 0;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (!s_sharedWake) {
    s_sharedWake = xSemaphoreCreateBinary();
    if (!s_sharedWake) {
      FWDIST_LOGLN("[fwdist] failed to create wake semaphore");
      state_ = State::Disabled;
      return;
    }
  }
  if (s_streamerCount >= (sizeof(s_streamers) / sizeof(s_streamers[0]))) {
    FWDIST_LOGLN("[fwdist] streamer registry full");
    state_ = State::Disabled;
    return;
  }
  s_streamers[s_streamerCount++] = this;
  FWDIST_LOGF("[fwdist] online; v=0x%08lx chunks=%u totalLen=%u\n",
                (unsigned long)lamp::FIRMWARE_VERSION,
                (unsigned)firmwareTotalChunks_,
                (unsigned)firmwareTotalLen_);
  // Loop-task stack HWM: computeShaPrefixOnce stack-allocates a 512 B SHA
  // scratch block on the loop stack (discoverImageLength's window is ~259 B).
  const UBaseType_t hwmBytes = uxTaskGetStackHighWaterMark(nullptr);
  FWDIST_LOGF("[fwdist] loop-task stack HWM: %u bytes free\n",
                (unsigned)hwmBytes);
#endif
}

bool FirmwareDistributor::isInProgress() const {
  return state_ == State::OfferSent || state_ == State::Streaming ||
         state_ == State::Finalizing;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

void FirmwareDistributor::startSharedStreaming() {
  if (s_sharedTask || s_streamerCount == 0) return;
  // Unpinned so the scheduler can place it on whichever core has slack.
  const BaseType_t ok = xTaskCreate(
      &FirmwareDistributor::streamingTaskTrampoline,
      "fwdist",
      kStreamingTaskStackSize,
      nullptr,
      kStreamingTaskPriority,
      &s_sharedTask);
  if (ok != pdPASS) {
    FWDIST_LOGLN("[fwdist] failed to create streaming task");
  }
}

bool FirmwareDistributor::readPartitionBytes(uint32_t offset, size_t len,
                                             uint8_t* buf) const {
  if (!runningPartition_ || !buf) return false;
  if (offset + len > runningPartition_->size) return false;
  const esp_err_t err = esp_partition_read(runningPartition_, offset, buf, len);
  return err == ESP_OK;
}

bool FirmwareDistributor::discoverImageLength(uint32_t* outLen) const {
  if (!runningPartition_ || !outLen) return false;
  return ::lamp::firmware::discoverSignedImageLength(
      [this](size_t off, size_t want, uint8_t* out) -> int {
        return readPartitionBytes(static_cast<uint32_t>(off), want, out)
                   ? static_cast<int>(want)
                   : -1;
      },
      runningPartition_->size, outLen);
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

  // Stream the signed region through SHA-256 in 512 B blocks: the partition is
  // at a physical offset, not a CPU address, so it can't be hashed in place.
  // SHA-256 is a streaming hash, so block size doesn't change the digest; a
  // small block keeps the boot-time scratch off the loop-task stack.
  constexpr size_t kBlock = 512;
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
  std::memcpy(sha256Full_, digest, 32);
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
  if (s_sharedWake) xSemaphoreGive(s_sharedWake);
}

void FirmwareDistributor::streamingTaskTrampoline(void* arg) {
  (void)arg;
  streamingTaskLoop();
}

// Blocks on the wake semaphore until there's work, then drains
// streamingTaskStep() until the state machine leaves an active state. Given
// from state-transition sites (initial OFFER, ACCEPT rx, REQ rewind) so the
// task can preempt the loop without waiting on the next idle poll boundary.
void FirmwareDistributor::streamingTaskLoop() {
  for (;;) {
    xSemaphoreTake(s_sharedWake, pdMS_TO_TICKS(kStreamingIdlePollMs));
    for (uint8_t i = 0; i < s_streamerCount; ++i) {
      // Re-read millis() per step; a hoisted now freezes the clock and the
      // tick() stall watchdog reads every chunk as overdue, failing the peer.
      while (s_streamers[i]->streamingTaskStep(millis())) {
        // Yield so the WiFi task can push frames over the air before the next
        // one is queued (see kStreamingChunkSpacingMs).
        vTaskDelay(pdMS_TO_TICKS(kStreamingChunkSpacingMs));
      }
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
  bool     quietHeldLocal = false;
  bool     sessionQuietArmedLocal = false;
  portENTER_CRITICAL(&stateMux_);
  s = state_;
  std::memcpy(targetMacLocal, targetMac_, 6);
  lastOfferLocal     = lastOfferSendMs_;
  offerRetriesLocal  = offerRetryCount_;
  nextChunkLocal     = nextChunkIdx_;
  totalChunksLocal   = totalChunks_;
  quietHeldLocal     = quietHeld_;
  sessionQuietArmedLocal = sessionQuietArmed_;
  portEXIT_CRITICAL(&stateMux_);

  if (s == State::OfferSent) {
    if (offerRetriesLocal >= kMaxOfferRetries) {
      // Out of retries; wait for ACCEPT (recv task) or kAcceptTimeoutMs (tick).
      return false;
    }
    const uint32_t sinceLast = nowMs - lastOfferLocal;
    if (sinceLast >= kOfferRetryIntervalMs) {
      // Bump under mux BEFORE sending so a fast Accept-rx interleave reads it.
      portENTER_CRITICAL(&stateMux_);
      offerRetryCount_++;
      portEXIT_CRITICAL(&stateMux_);
      sendOfferFrame(targetMacLocal, nowMs, /*isRetry=*/true);
      return true;
    }
    const uint32_t waitMs = kOfferRetryIntervalMs - sinceLast;
    // xSemaphoreTake (not vTaskDelay) so a state-pivoting recv event wakes the
    // task immediately instead of burning ~200ms of dead air after ACCEPT.
    xSemaphoreTake(s_sharedWake, pdMS_TO_TICKS(waitMs));
    return true;
  }

  if (s == State::Streaming) {
    // Deferred radio teardown + quiet entry, once per session, before chunk 0.
    // Safe task context (unlike the recv task that sets Streaming) and the radio
    // is down for the chunk flood. An inherited hold (quietHeldLocal) already
    // tore it down. quietHeld_ is state-disjoint here: the loop-task Idle reap
    // only clears it in State::Idle, which can't run while this session streams.
    if (!sessionQuietArmedLocal) {
      if (!quietHeldLocal) {
        ::lamp::ota_quiet_mode::enterQuiet(/*tearDownRadio=*/true,
                                           /*visible=*/fsHooks_ == nullptr);
      }
      portENTER_CRITICAL(&stateMux_);
      quietHeld_         = true;
      quietHoldUntilMs_  = 0;
      sessionQuietArmed_ = true;
      portEXIT_CRITICAL(&stateMux_);
#ifdef LAMP_DEBUG
      const UBaseType_t hwmBytes = uxTaskGetStackHighWaterMark(nullptr);
      FWDIST_LOGF("[fwdist] streaming-task stack HWM after teardown: "
                  "%u bytes free\n",
                  (unsigned)hwmBytes);
#endif
    }
    if (nextChunkLocal >= totalChunksLocal) {
      // Drained: transition to Finalizing and emit DONE outside the mux.
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
      // No more chunk work; idle. tick() owns the FINALIZE timeout, the recv
      // task wakes the streamer on RESULT.
      return false;
    }
    const int rc = streamOneChunk(nowMs);
    if (rc == 1) {
      // NO_MEM.
      vTaskDelay(pdMS_TO_TICKS(kStreamingQueueBackoffMs));
      return true;
    }
    if (rc == 2) {
      // Partition read failure already aborted the session.
      return false;
    }
    return true;
  }

  return false;
}

// Single-chunk emit + index advance. Returns 0 = queued, advanced; 1 = NO_MEM,
// caller delays + retries; 2 = partition read failure, session aborted.
int FirmwareDistributor::streamOneChunk(uint32_t nowMs) {
  // Snapshot index + target under mux. Don't advance nextChunkIdx_ until the
  // frame is queued: a concurrent onReq may rewind it before the send.
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

  uint8_t scratch[lamp_protocol::FW_CHUNK_SIZE_MAX];
  const uint32_t offset =
      static_cast<uint32_t>(chunkIdx) * sessionChunkSize_;
  // Last chunk is short when totalLen isn't a multiple of sessionChunkSize_.
  size_t want = sessionChunkSize_;
  if (offset >= firmwareTotalLen_) {
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
      targetProtocolVersion_,
      fsHooks_ ? fsHooks_->chunkType : lamp_protocol::MSG_FW_CHUNK);
  if (!framed) return 1;

  // Send outside the mux. sendFrame returns false on NO_MEM/queue-full.
  if (!transport_->sendFrame(buf, framed)) {
#ifdef LAMP_DEBUG
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

  // Advance nextChunkIdx_ only if it still points at the chunk just sent; a
  // concurrent onReq may have rewound it, in which case the next iteration
  // sends from there.
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
    // Smart-REQ resume: after serving the requested chunks, jump back to the
    // forward-progress position so the stream doesn't repeat what the receiver
    // has (set up in onReq).
    if (resumeChunkIdx_ != 0 && nextChunkIdx_ >= reqEndIdx_ &&
        nextChunkIdx_ < resumeChunkIdx_) {
      resumeToLog       = resumeChunkIdx_;
      nextChunkIdx_     = resumeChunkIdx_;
      resumeChunkIdx_   = 0;
      reqEndIdx_        = 0;
      resumedToFwd      = true;
    }
    // Monotonic high water for the indicator. The rewind paths (REQ + stall
    // recovery) intentionally don't touch this; forward progress only.
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

// Drives ACCEPT/FINALIZE timeouts, the stall watchdog, and the tombstone reaper.
void FirmwareDistributor::tick(uint32_t nowMs) {
  if (state_ == State::Disabled) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Refresh from millis(); the passed nowMs predates slow loop work and a stale
  // value underflows the uint32_t subtractions, firing timeouts instantly.
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

#ifdef LAMP_DEBUG
  // Heartbeat: state + dwell distinguishes a stuck session from Idle-but-not-
  // offering when diagnosing a wedge.
  static uint32_t s_hbMs = 0;
  if (nowMs - s_hbMs >= 2000) {
    s_hbMs = nowMs;
    static const char* const kStateName[] = {
        "Disabled", "Idle", "OfferSent", "Streaming", "Finalizing", "Failed", "Done"};
    FWDIST_LOGF("[fwdist] hb state=%s dwell=%ums\n",
                kStateName[(uint8_t)s <= 6 ? (uint8_t)s : 0],
                (unsigned)(nowMs - stateEnteredMsLocal));
  }
#endif

  switch (s) {
    case State::Idle: {
      // quietHoldUntilMs_ == 0 is the "no exit pending" sentinel; without the
      // != 0 guard a tick during a live session would exitQuiet early and
      // flicker the strip between the indicator and the base color.
      if (quietHeld_ && quietHoldUntilMs_ != 0 && nowMs >= quietHoldUntilMs_) {
        ::lamp::ota_quiet_mode::exitQuiet();
        quietHeld_         = false;
        quietHoldUntilMs_  = 0;
        lastSessionValid_  = false;
      }
      break;
    }

    case State::OfferSent: {
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
      // Streaming runs on the dedicated task; tick() only checks the stall
      // watchdog (no progress in kChunkResendMs bumps the retry counter;
      // exceeded fails the peer).
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
      // Defer exitQuiet to keep the indicator up through the gap to the next
      // session, but only when quiet is actually held: a session that failed
      // before its first Streaming step never entered quiet. The Idle case
      // expires the hold if nothing arms within kInterSessionQuietHoldMs; a new
      // emitOffer inside the window inherits the held quiet and clears the exit.
      if (quietHeld_) {
        quietHoldUntilMs_ = nowMs + kInterSessionQuietHoldMs;
      }
      break;

    case State::Disabled:
      break;
  }
}

void FirmwareDistributor::considerPeerForOta(const uint8_t peerMac[6],
                                             uint32_t peerVersion,
                                             uint8_t peerProtocolVersion,
                                             uint32_t nowMs,
                                             const char* peerFwChannel,
                                             uint16_t peerMaxChunk,
                                             int8_t peerRssi) {
  if (state_ != State::Idle) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "state=%u (not Idle)\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion, (unsigned)state_);
    return;
  }
  if (!transport_) return;
  // Skip until a HELLO has arrived (peerProtocolVersion 0), so an OFFER never
  // goes out at an unknown version.
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
  // Skip peers below the OTA signal floor: a weak direct hop thrashes/fails
  // regardless of chunk size. Unknown RSSI (-127 sentinel) isn't gated;
  // cascade OTA reaches this peer later via a nearer upgraded lamp.
  if (peerRssi != -127 && peerRssi < lamp_protocol::kOtaMinRssiDbm) {
    FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X v=0x%08X skip: "
                "rssi=%d below floor %d\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5],
                (unsigned)peerVersion, (int)peerRssi,
                (int)lamp_protocol::kOtaMinRssiDbm);
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
  // FS distributor uses its own staleness gate; firmware uses otaAcceptable.
  if (!fsHooks_) {
    if (peerFwChannel && peerFwChannel[0] != '\0') {
      // Known channel: ask "would the peer accept this firmware?" by calling
      // otaAcceptable with the peer as receiver and this lamp as the offer side.
      if (!otaAcceptable(peerFwChannel, peerVersion,
                         lamp::FIRMWARE_CHANNEL_STR, lamp::FIRMWARE_VERSION)) {
        FWDIST_LOGF("[fwdist] consider %02X:%02X:%02X:%02X:%02X:%02X skip: "
                    "otaAcceptable(peerCh=%s peerV=0x%08X ourCh=%s ourV=0x%08X)=false\n",
                    peerMac[0], peerMac[1], peerMac[2],
                    peerMac[3], peerMac[4], peerMac[5],
                    peerFwChannel, (unsigned)peerVersion,
                    lamp::FIRMWARE_CHANNEL_STR, (unsigned)lamp::FIRMWARE_VERSION);
        return;
      }
    } else {
      // Unknown/empty channel (older peer with no channel TLV): fall back to
      // version-only gate. Receiver's otaAcceptable is the downstream backstop.
      if (peerVersion >= lamp::FIRMWARE_VERSION) return;
    }
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
  // An unknown-channel peer can still reach here; the receiver's onOfferOnLoop
  // silent-drop is the backstop (the OFFER times out into the backoff ring).
  targetProtocolVersion_ = peerProtocolVersion;
  // peerMaxChunk 0 = peer doesn't advertise HELLO_TLV_FW_MAX_CHUNK (older
  // firmware, or never-OTA-receiving peer) → the baseline every receiver
  // accepts. Otherwise cap at FW_CHUNK_SIZE_MAX in case a peer overstates.
  const uint16_t cappedPeerMaxChunk =
      peerMaxChunk < lamp_protocol::FW_CHUNK_SIZE_MAX
          ? peerMaxChunk
          : lamp_protocol::FW_CHUNK_SIZE_MAX;
  sessionChunkSize_ = peerMaxChunk > 0 ? cappedPeerMaxChunk
                                       : lamp_protocol::FW_CHUNK_SIZE_BASELINE;
  emitOffer(peerMac, peerVersion, nowMs);
}

void FirmwareDistributor::resetSession() {
  std::memset(targetMac_, 0, 6);
  targetProtocolVersion_ = 0;
  sessionChunkSize_ = lamp_protocol::FW_CHUNK_SIZE_BASELINE;
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
  sessionQuietArmed_ = false;
}

void FirmwareDistributor::captureLastSession() {
  std::memcpy(lastSessionPeerMac_, targetMac_, 6);
  lastSessionTotalChunks_ = totalChunks_;
  lastSessionValid_       = true;
}

bool FirmwareDistributor::getLastSession(uint8_t outMac[6],
                                        uint16_t& outTotalChunks) const {
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
    if (p.persistent) return true;
    if (nowMs < p.backoffUntilMs) return true;
  }
  return false;
}

void FirmwareDistributor::notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                                          uint32_t durationMs,
                                          bool persistent) {
  for (size_t i = 0; i < kPenaltyRingSize; ++i) {
    if (penalties_[i].used && macsEqual(penalties_[i].mac, mac)) {
      penalties_[i].backoffUntilMs = nowMs + durationMs;
      penalties_[i].persistent     = persistent;
      return;
    }
  }

  // Slot priority for a new mac: free > expired transient > active transient.
  // A persistent (cross-variant-block) slot is never picked here, so a later
  // transient penalty can't evict an earlier permanent block.
  int freeSlot = -1, expiredTransient = -1, activeTransient = -1, oldestPersistent = -1;
  for (size_t i = 0; i < kPenaltyRingSize; ++i) {
    PeerPenalty& p = penalties_[i];
    if (!p.used) {
      if (freeSlot < 0) freeSlot = static_cast<int>(i);
    } else if (!p.persistent) {
      if (nowMs >= p.backoffUntilMs) {
        if (expiredTransient < 0) expiredTransient = static_cast<int>(i);
      } else if (activeTransient < 0) {
        activeTransient = static_cast<int>(i);
      }
    } else if (oldestPersistent < 0 ||
               p.backoffUntilMs < penalties_[oldestPersistent].backoffUntilMs) {
      oldestPersistent = static_cast<int>(i);
    }
  }

  int slot = freeSlot >= 0 ? freeSlot
           : expiredTransient >= 0 ? expiredTransient
           : activeTransient;
  if (slot < 0) {
    // Ring is entirely persistent blocks. A transient penalty re-arms on its
    // next failure, so skip it rather than evict a permanent block; a new
    // permanent block replaces the oldest one (bounded, least-bad choice).
    if (!persistent) return;
    slot = oldestPersistent;
  }

  penalties_[slot].used           = true;
  penalties_[slot].persistent     = persistent;
  std::memcpy(penalties_[slot].mac, mac, 6);
  penalties_[slot].backoffUntilMs = nowMs + durationMs;
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

void FirmwareDistributor::recordPeerBlocklist(uint32_t nowMs) {
  bool nonzero = false;
  for (int i = 0; i < 6; ++i) {
    if (targetMac_[i] != 0) { nonzero = true; break; }
  }
  if (nonzero) notePeerBackoff(targetMac_, nowMs, 0, /*persistent=*/true);
}

void FirmwareDistributor::emitOffer(const uint8_t targetMac[6],
                                    uint32_t peerVersion, uint32_t nowMs) {
  if (!transport_) return;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (!runningPartition_ || !shaPrefixReady_) return;
#endif
  if (firmwareTotalLen_ == 0 || firmwareTotalChunks_ == 0) return;
  (void)peerVersion;

  // Init session state under the mux so recv + streaming tasks see a consistent
  // snapshot. Re-check state_ == Idle INSIDE the mux to close the
  // considerPeerForOta TOCTOU race (two ticks could both see Idle outside it).
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
  // Recomputed per session: sessionChunkSize_ (set just before this call, in
  // considerPeerForOta) can differ from the chunk size begin() assumed, so
  // firmwareTotalChunks_ (that stale baseline count) isn't reused here.
  totalChunks_      = static_cast<uint16_t>(
      (firmwareTotalLen_ + sessionChunkSize_ - 1) / sessionChunkSize_);
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
  // Tentative OfferSent BEFORE the send so a near-instant ACCEPT on the recv
  // task is matched; rolled back below if the send fails.
  state_ = State::OfferSent;
  stateEnteredMs_ = nowMs;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif

  // Quiet mode + radio teardown are deferred to the streaming task on the first
  // Streaming step (see streamingTaskStep): enterQuiet's BLE-host + webserver
  // shutdown isn't safe on the recv task where ACCEPT lands, and keeping adv/scan
  // live through OfferSent lets the first ACCEPT arrive before the radio drops.
  // An inherited hold (quietHeld_) means the radio is already down; just cancel
  // its pending exit.
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
    // Send failed before the session got off the ground (likely transport, not
    // peer cadence). Only drop quiet if this distributor actually holds it (an
    // inherited hold from a prior session): a bare exitQuiet would decrement a
    // concurrent receiver session's refcount and knock the strip out of quiet
    // mid-receive.
    if (quietHeld_) {
      ::lamp::ota_quiet_mode::exitQuiet();
      quietHeld_ = false;
    }
    quietHoldUntilMs_ = 0;
    return;
  }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // The streaming task owns OFFER retries from here.
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
  uint16_t chunkSizeLocal;
  uint8_t  sha[8];
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portENTER_CRITICAL(&stateMux_);
#endif
  sessionOfferSeqLocal = sessionOfferSeq_;
  sessionVersionLocal  = sessionVersion_;
  sessionTotalLenLocal = sessionTotalLen_;
  totalChunksLocal     = totalChunks_;
  chunkSizeLocal       = sessionChunkSize_;
  std::memcpy(sha, sha256Prefix_, 8);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  portEXIT_CRITICAL(&stateMux_);
#endif

  uint8_t buf[lamp_protocol::FW_OFFER_AUTH_SIZE];
  const char* channel = lamp::FIRMWARE_CHANNEL_STR;
  const size_t channelLen = channel ? std::strlen(channel) : 0;
  // Firmware and FS OTA both emit the auth trailer (digest + signature) once
  // authReady_ so the receiver verifies before streaming; FS binds the same
  // fw.lsig signature over its manifest digest. A pre-auth image sends the
  // legacy trailerless OFFER.
  const uint8_t* digestArg = authReady_ ? sha256Full_ : nullptr;
  const uint8_t* sigArg    = authReady_ ? imageSignature_ : nullptr;
  const size_t n = lamp_protocol::buildFwOffer(
      buf, sizeof(buf), sessionOfferSeqLocal,
      cachedSrcMac_, targetMac,
      sessionVersionLocal, sessionTotalLenLocal, chunkSizeLocal,
      channel, channelLen,
      sha, kFwFooterLenV1, totalChunksLocal,
      targetProtocolVersion_,
      fsHooks_ ? fsHooks_->offerType : lamp_protocol::MSG_FW_OFFER,
      digestArg, sigArg);
  if (!n) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    FWDIST_LOGLN("[fwdist] buildFwOffer failed");
#endif
    return false;
  }
  const bool sent = transport_->sendFrame(buf, n);
  // Bump lastOfferSendMs_ regardless of send success: a transient NO_MEM that
  // left it unchanged would make the next streaming tick burn the retry budget
  // in a tight loop. Apply the cooldown either way.
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
      sha, kFwFooterLenV1, targetProtocolVersion_,
      fsHooks_ ? fsHooks_->doneType : lamp_protocol::MSG_FW_DONE);
  if (!n) return;

  // First attempt + up to kMaxDoneRetries follow-ups. Bail if state pivots out
  // of Finalizing (onResult, onReq, or the tick() finalize timeout).
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
      // xSemaphoreTake (not vTaskDelay) so onResult's wakeStreamingTask
      // preempts the wait once RESULT lands, instead of sending more DONE
      // frames while mid-sleep.
      xSemaphoreTake(s_sharedWake, pdMS_TO_TICKS(kDoneRetryIntervalMs));
    }
#endif
  }
  (void)nowMs;
}

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
    // Snapshot reasons under the mux for a consistent post-exit log.
    const uint8_t  logState        = static_cast<uint8_t>(state_);
    const bool     logMacMismatch  = !macsEqual(a.sourceMac, targetMac_);
    const uint16_t logRxSeq        = a.offerSeq;
    const uint16_t logExpectedSeq  = sessionOfferSeq_;
    const uint32_t logRxVersion    = a.version;
    const uint32_t logExpectedVer  = sessionVersion_;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&stateMux_);
    // Log only in OfferSent, when an ACCEPT is expected; drops elsewhere are routine.
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
    // Offers go only to peers read as behind; DeclineAlreadyCurrent from one
    // of them means it can't actually accept the local variant (a genuine
    // same-variant behind peer replies Accept). Block it instead of retrying
    // on a timer.
    if (a.status == lamp_protocol::FwAcceptStatus::DeclineAlreadyCurrent) {
      recordPeerBlocklist(nowMs);
    } else {
      recordPeerFailure(nowMs);
    }
    resetSession();
    state_ = State::Failed;
    stateEnteredMs_ = nowMs;
  } else {
    state_ = State::Streaming;
    stateEnteredMs_ = nowMs;
    // Stamp lastSentMs_ to now so the tick() stall watchdog gives the streaming
    // task a fair kChunkResendMs window for the first chunk. resetSession() left
    // it 0, which tick() reads as instantly stalled and burns the retry budget
    // before the streaming task sends once (worst under BLE coex).
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
    // Snapshot reasons under the mux for a consistent post-exit log.
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
  // Per-session REQ budget. Excess REQs mean catastrophic loss or a rewind-loop
  // attempt; either way, back off the peer and tombstone the session.
  // recordPeerFailure reads targetMac_, so it MUST run before resetSession().
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
  // Smart rewind: save the forward-progress position in resumeChunkIdx_ so the
  // streaming task can jump back to forward emit after serving the requested
  // chunks, instead of re-streaming everything from firstChunkIdx the receiver
  // already has. Saved only on the FIRST rewind after a forward run.
  const uint16_t newReqEnd = r.firstChunkIdx + r.chunkCount;
  if (resumeChunkIdx_ == 0 &&
      nextChunkIdx_ > newReqEnd) {
    resumeChunkIdx_ = nextChunkIdx_;
    reqEndIdx_      = newReqEnd;
  } else if (resumeChunkIdx_ != 0 && newReqEnd > reqEndIdx_) {
    // Stacked REQ during an in-flight rewind: extend the window end so the
    // jump-forward waits for the new REQ's tail, otherwise the gap between
    // ranges gets re-REQ'd by the receiver's stall watchdog.
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
    captureLastSession();
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
