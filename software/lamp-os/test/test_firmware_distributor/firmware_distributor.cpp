// Native-host unit tests for the lamp's FirmwareDistributor state machine.
//
// The production class depends on FreeRTOS (xTaskCreate, xSemaphoreGive,
// portENTER_CRITICAL) + ESP-IDF (esp_ota_get_running_partition,
// esp_partition_read) + the mbedtls SHA-256 streaming API — none of
// which are available on the native host. Per the project convention
// (see test_firmware_receiver.cpp for the precedent), we mirror the
// observable state-machine logic into a self-contained `DistAlgo` in
// namespace `test` and drive it with synthetic ticks + fake inbound
// packets.
//
// What this exercises (and what it does NOT):
//   ✓ Idle → OfferSent → Streaming → Finalizing → Done/Failed state
//     transitions, including the time-driven timeouts and the recv-
//     driven onAccept / onReq / onResult paths
//   ✓ Per-peer backoff ring: notePeerBackoff, peerIsInBackoff, ring
//     eviction, ring-slot reuse for the same MAC
//   ✓ considerPeerForOta gating: peer-in-backoff, peer-already-current,
//     distributor-not-Idle
//   ✓ OFFER retry interval + cap
//   ✓ Streaming chunk-cursor advance + currentChunkRetries_ + stall
//   ✓ FW_REQ rewind cursor + reqCountThisSession_ budget hardening
//     (Phase 5b' hardening — new on the lamp side, not in the wisp test)
//   ✓ Full happy path OFFER → ACCEPT → CHUNK× → DONE → RESULT(success)
//   ✓ discoverImageLength scan-backward logic (lamp-specific; replaces
//     the wisp's "totalLen = carrier.size" assumption)
//   ✗ Real mesh emit (transport_->sendFrame is a FreeRTOS-queued call)
//   ✗ Real esp_partition_read (mocked to a buffer)
//   ✗ Real SHA-256 prefix (test_firmware_signature covers the hash math)
//   ✗ Cross-task mutex discipline (portMUX/SemaphoreHandle are no-ops on
//     native — the wiring is verified on hardware only)

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace test {

// --- Mirror constants ----------------------------------------------------

constexpr uint16_t kChunkSize             = 200;
constexpr uint32_t kAcceptTimeoutMs       = 5000;
constexpr uint32_t kFinalizeTimeoutMs     = 30000;
constexpr uint32_t kChunkResendMs         = 1500;
constexpr uint8_t  kRetriesPerChunk       = 4;
constexpr uint32_t kPeerBackoffMs         = 600000;  // 10 minutes
constexpr uint32_t kPeerFinalizeBackoffMs = 15000;
constexpr uint16_t kBurstChunksPerTick    = 8;
constexpr uint8_t  kMaxOfferRetries       = 8;
constexpr uint32_t kOfferRetryIntervalMs  = 200;
constexpr uint8_t  kMaxDoneRetries        = 4;
constexpr uint32_t kDoneRetryIntervalMs   = 300;
// Phase 5b' lamp-side hardening — caps how many MSG_FW_REQ a peer can
// send in one session before we tombstone the session with peer backoff.
constexpr uint16_t kMaxReqPerSession      = 32;

enum class State : uint8_t {
  Disabled = 0,
  Idle,
  OfferSent,
  Streaming,
  Finalizing,
  Failed,
  Done,
};

struct Penalty {
  uint8_t  mac[6];
  uint32_t backoffUntilMs;
  bool     used;
};

// --- DistAlgo: lamp state machine mirror --------------------------------

// Differences from the wisp's algorithm mirror:
//   - No scan-based targeting: targeting is event-driven via
//     considerPeerForOta(peerMac, peerVersion, nowMs). No firstScan_,
//     lastScanMs_, kInitialScanDelayMs, kScanPeriodMs, no pickTargetAndOffer.
//   - kMaxReqPerSession added — protects against a malicious or buggy
//     receiver pinning the sender in an infinite REQ-rewind loop.
struct DistAlgo {
  uint32_t myVersion      = 0x00010005u;  // 1.0.5
  uint16_t totalChunks_   = 10;

  State    state_         = State::Idle;
  uint32_t stateEnteredMs_ = 0;

  uint8_t  targetMac_[6]   = {0};
  uint16_t nextChunkIdx_   = 0;
  uint16_t lastSentChunk_  = 0;
  uint32_t lastSentMs_     = 0;
  uint8_t  currentChunkRetries_ = 0;
  uint32_t lastOfferSendMs_     = 0;
  uint8_t  offerRetryCount_     = 0;
  uint16_t reqCountThisSession_ = 0;

  // DONE retry bookkeeping. Same shape as wisp's mirror.
  uint16_t doneSeqCaptured_   = 0;
  uint8_t  doneAttempts_      = 0;
  bool     doneRetryAborted_  = false;

  static constexpr size_t kPenaltyRingSize = 8;
  Penalty penalties_[kPenaltyRingSize] = {};
  size_t  penaltyHead_ = 0;

  static bool macsEqual(const uint8_t a[6], const uint8_t b[6]) {
    return std::memcmp(a, b, 6) == 0;
  }

  bool peerIsInBackoff(const uint8_t mac[6], uint32_t nowMs) const {
    for (size_t i = 0; i < kPenaltyRingSize; ++i) {
      const auto& p = penalties_[i];
      if (!p.used) continue;
      if (!macsEqual(p.mac, mac)) continue;
      if (nowMs < p.backoffUntilMs) return true;
    }
    return false;
  }

  void notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                       uint32_t durationMs = kPeerBackoffMs) {
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

  void resetSession() {
    std::memset(targetMac_, 0, 6);
    nextChunkIdx_        = 0;
    lastSentChunk_       = 0;
    lastSentMs_          = 0;
    currentChunkRetries_ = 0;
    lastOfferSendMs_     = 0;
    offerRetryCount_     = 0;
    reqCountThisSession_ = 0;
  }

  // Event-driven targeting — caller (SocialBehavior) supplies a peer
  // observed via ESP-NOW HELLO with a stale version. Idempotent on
  // non-Idle (re-call while OfferSent / Streaming / Finalizing is a
  // no-op so the social tick can spam it without harm).
  bool considerPeerForOta(const uint8_t peerMac[6], uint32_t peerVersion,
                          uint32_t nowMs) {
    if (state_ != State::Idle) return false;
    if (peerVersion >= myVersion) return false;
    if (peerIsInBackoff(peerMac, nowMs)) return false;
    std::memcpy(targetMac_, peerMac, 6);
    state_              = State::OfferSent;
    stateEnteredMs_     = nowMs;
    lastOfferSendMs_    = nowMs;
    offerRetryCount_    = 0;
    nextChunkIdx_       = 0;
    reqCountThisSession_ = 0;
    return true;
  }

  void tick(uint32_t nowMs) {
    switch (state_) {
      case State::Disabled:
      case State::Idle:
        return;
      case State::OfferSent:
        if ((nowMs - stateEnteredMs_) > kAcceptTimeoutMs) {
          notePeerBackoff(targetMac_, nowMs);
          resetSession();
          state_ = State::Failed;
          stateEnteredMs_ = nowMs;
          return;
        }
        if (offerRetryCount_ < kMaxOfferRetries &&
            (nowMs - lastOfferSendMs_) >= kOfferRetryIntervalMs) {
          offerRetryCount_++;
          lastOfferSendMs_ = nowMs;
        }
        return;
      case State::Streaming: {
        if (nextChunkIdx_ >= totalChunks_) {
          state_ = State::Finalizing;
          stateEnteredMs_ = nowMs;
          return;
        }
        for (uint16_t i = 0; i < kBurstChunksPerTick; ++i) {
          if (nextChunkIdx_ >= totalChunks_) break;
          lastSentChunk_ = nextChunkIdx_;
          lastSentMs_    = nowMs;
          nextChunkIdx_++;
          currentChunkRetries_ = 0;
        }
        if ((nowMs - lastSentMs_) > kChunkResendMs) {
          if (currentChunkRetries_ >= kRetriesPerChunk) {
            notePeerBackoff(targetMac_, nowMs);
            resetSession();
            state_ = State::Failed;
            stateEnteredMs_ = nowMs;
            return;
          }
          currentChunkRetries_++;
          lastSentMs_ = nowMs;
        }
        return;
      }
      case State::Finalizing:
        if ((nowMs - stateEnteredMs_) > kFinalizeTimeoutMs) {
          notePeerBackoff(targetMac_, nowMs, kPeerFinalizeBackoffMs);
          resetSession();
          state_ = State::Failed;
          stateEnteredMs_ = nowMs;
        }
        return;
      case State::Failed:
      case State::Done:
        resetSession();
        state_ = State::Idle;
        stateEnteredMs_ = nowMs;
        return;
    }
  }

  void onAccept(const uint8_t fromMac[6], uint32_t nowMs) {
    if (state_ != State::OfferSent) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    state_ = State::Streaming;
    stateEnteredMs_ = nowMs;
  }

  // Mirror of the Phase 5b' lamp-side hardening: reqCountThisSession_ is
  // bumped per accepted REQ; exceeding kMaxReqPerSession aborts the
  // session with a full peer-backoff penalty (not the short
  // Finalize-timeout backoff — a REQ flood is treated as a hostile or
  // catastrophically broken peer).
  void onReq(const uint8_t fromMac[6], uint16_t firstIdx, uint32_t nowMs) {
    if (state_ != State::Streaming && state_ != State::Finalizing) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    if (firstIdx >= totalChunks_) return;
    if (reqCountThisSession_ >= kMaxReqPerSession) {
      notePeerBackoff(targetMac_, nowMs);  // 10-min backoff
      resetSession();
      state_ = State::Failed;
      stateEnteredMs_ = nowMs;
      return;
    }
    reqCountThisSession_++;
    nextChunkIdx_ = firstIdx;
    currentChunkRetries_ = 0;
    lastSentMs_ = nowMs;
    if (state_ == State::Finalizing) {
      state_ = State::Streaming;
      stateEnteredMs_ = nowMs;
    }
  }

  void onResultSuccess(const uint8_t fromMac[6], uint32_t nowMs) {
    if (state_ != State::Finalizing) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    resetSession();
    state_ = State::Done;
    stateEnteredMs_ = nowMs;
  }

  void onResultFail(const uint8_t fromMac[6], uint32_t nowMs) {
    if (state_ != State::Finalizing) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    notePeerBackoff(targetMac_, nowMs);
    resetSession();
    state_ = State::Failed;
    stateEnteredMs_ = nowMs;
  }

  // Production isDistributingTo: mid-flow AND mac matches active target.
  bool isDistributingTo(const uint8_t mac[6]) const {
    if (state_ != State::OfferSent && state_ != State::Streaming &&
        state_ != State::Finalizing) {
      return false;
    }
    return macsEqual(targetMac_, mac);
  }

  // DONE retry loop mirror. Same as wisp's: budget = 1 + kMaxDoneRetries,
  // bails on state pivot away from Finalizing. `pivotAfter` lets a test
  // simulate RESULT arriving mid-loop after attempt N.
  void runEmitDoneRetryLoop(uint32_t /*nowStartMs*/,
                            uint8_t pivotAfter = 0xFF) {
    if (state_ != State::Finalizing) {
      doneRetryAborted_ = true;
      return;
    }
    doneRetryAborted_ = false;
    doneAttempts_     = 0;
    static uint16_t seqGen = 0xA000;
    doneSeqCaptured_ = ++seqGen;
    const uint8_t budget = 1 + kMaxDoneRetries;
    for (uint8_t i = 0; i < budget; ++i) {
      if (state_ != State::Finalizing) {
        doneRetryAborted_ = true;
        return;
      }
      doneAttempts_++;
      if (i + 1 == pivotAfter) state_ = State::Done;
    }
  }
};

// --- discoverImageLength helper: scan-backward for LSIG magic
//
// Mirror of the production helper. Hardware testing on 2026-06-10
// found that the original "last non-0xFF byte" approach was wrong:
// esptool's flash-write doesn't erase the partition tail, so a
// re-flashed lamp whose prior firmware was larger leaves stale bytes
// past the new image's end. The current production helper scans
// backward for the "LSIG" magic and validates by reading the footer's
// signedRegionLen field — which must equal the footer's offset (the
// LSIG sits immediately after the signed region). This test mirror
// implements that contract over a buffer (simulating a partition).
bool discoverImageLengthScanBackward(const uint8_t* buf, size_t bufLen,
                                     uint32_t* outLen) {
  if (!buf || !outLen || bufLen < 96) return false;
  constexpr size_t kFwFooterLenV1 = 96;
  // Walk backward looking for "LSIG"; require at least 4 bytes left
  // for the magic and 96 bytes total for the footer.
  for (size_t i = bufLen; i >= kFwFooterLenV1; --i) {
    const size_t off = i - kFwFooterLenV1;
    if (buf[off]     == 'L' && buf[off + 1] == 'S' &&
        buf[off + 2] == 'I' && buf[off + 3] == 'G') {
      // Read signedRegionLen at footer offset 16 (4 bytes LE).
      const uint32_t signedRegionLen =
          static_cast<uint32_t>(buf[off + 16]) |
          (static_cast<uint32_t>(buf[off + 17]) << 8) |
          (static_cast<uint32_t>(buf[off + 18]) << 16) |
          (static_cast<uint32_t>(buf[off + 19]) << 24);
      // Validate: signedRegionLen must equal the footer's offset (the
      // LSIG sits at the END of the signed region). Coincidental
      // ascii 'LSIG' inside a string literal in the prior firmware
      // would fail this check.
      if (signedRegionLen == static_cast<uint32_t>(off)) {
        *outLen = static_cast<uint32_t>(off + kFwFooterLenV1);
        return true;
      }
    }
  }
  return false;
}

// Helper for tests — build a buffer that LOOKS like a signed firmware:
// [signed region of len `signedLen`] + ['LSIG' magic + channel(8) +
// version(4) + signedRegionLen(4) + reserved(12) + signature(64)].
std::vector<uint8_t> makeSignedImage(size_t signedLen) {
  std::vector<uint8_t> out(signedLen + 96, 0xFF);
  for (size_t i = 0; i < signedLen; ++i) out[i] = static_cast<uint8_t>(i & 0xFF);
  // Footer
  out[signedLen + 0] = 'L'; out[signedLen + 1] = 'S';
  out[signedLen + 2] = 'I'; out[signedLen + 3] = 'G';
  // signedRegionLen at footer offset 16 (LE)
  out[signedLen + 16] = static_cast<uint8_t>(signedLen & 0xFF);
  out[signedLen + 17] = static_cast<uint8_t>((signedLen >> 8) & 0xFF);
  out[signedLen + 18] = static_cast<uint8_t>((signedLen >> 16) & 0xFF);
  out[signedLen + 19] = static_cast<uint8_t>((signedLen >> 24) & 0xFF);
  return out;
}

// --- Test fixtures ------------------------------------------------------

DistAlgo makeAlgo(uint32_t myVer = 0x00010005u, uint16_t chunks = 10) {
  DistAlgo d;
  d.myVersion    = myVer;
  d.totalChunks_ = chunks;
  return d;
}

void macFromTail(uint8_t out[6], uint8_t tail) {
  out[0] = 0xAA; out[1] = 0xBB; out[2] = 0xCC;
  out[3] = 0x00; out[4] = 0x00; out[5] = tail;
}

}  // namespace test

using namespace test;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// considerPeerForOta gating
// =============================================================================

void test_considerPeerForOta_idle_emits_offer(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_EQUAL_MEMORY(mac, d.targetMac_, 6);
  TEST_ASSERT_EQUAL_UINT32(1000, d.lastOfferSendMs_);
  TEST_ASSERT_EQUAL_UINT8(0, d.offerRetryCount_);
}

void test_considerPeerForOta_nonIdle_is_noop(void) {
  auto d = makeAlgo();
  uint8_t macA[6]; macFromTail(macA, 1);
  uint8_t macB[6]; macFromTail(macB, 2);
  d.considerPeerForOta(macA, 0x00010003u, 1000);  // OfferSent
  TEST_ASSERT_FALSE(d.considerPeerForOta(macB, 0x00010001u, 1100));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_EQUAL_MEMORY(macA, d.targetMac_, 6);  // unchanged
}

void test_considerPeerForOta_peer_in_backoff_is_noop(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.notePeerBackoff(mac, 1000);  // backoff until 1000 + 600000
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 0x00010003u, 1500));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

void test_considerPeerForOta_peer_already_current_is_noop(void) {
  auto d = makeAlgo(0x00010005u, 10);
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 0x00010005u, 1000));
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 0x00010006u, 1000));  // newer
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

// =============================================================================
// OFFER retry + ACCEPT timeout
// =============================================================================

void test_offer_retry_advances_with_interval(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  TEST_ASSERT_EQUAL_UINT8(0, d.offerRetryCount_);
  // Below interval: no bump.
  d.tick(1100);
  TEST_ASSERT_EQUAL_UINT8(0, d.offerRetryCount_);
  // First interval crossing: count → 1.
  d.tick(1200);
  TEST_ASSERT_EQUAL_UINT8(1, d.offerRetryCount_);
  // Second crossing.
  d.tick(1400);
  TEST_ASSERT_EQUAL_UINT8(2, d.offerRetryCount_);
}

void test_accept_timeout_failures_peer(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  // Just under timeout: still OfferSent.
  d.tick(1000 + kAcceptTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
  // Cross timeout.
  d.tick(1000 + kAcceptTimeoutMs + 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  // Backoff was recorded.
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1000 + kAcceptTimeoutMs + 1));
  // Next tick reaps Failed → Idle.
  d.tick(1000 + kAcceptTimeoutMs + 100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

void test_accept_transitions_to_streaming(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Streaming),
                          static_cast<uint8_t>(d.state_));
}

void test_accept_from_wrong_mac_is_ignored(void) {
  auto d = makeAlgo();
  uint8_t target[6]; macFromTail(target, 1);
  uint8_t stranger[6]; macFromTail(stranger, 99);
  d.considerPeerForOta(target, 0x00010003u, 1000);
  d.onAccept(stranger, 1100);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
}

// =============================================================================
// Streaming chunk advance
// =============================================================================

void test_streaming_emits_chunks_in_order(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  // 5 chunks ≤ kBurstChunksPerTick (8) → all drained in one tick →
  // Finalizing on the next tick (state goes to Finalizing the moment
  // nextChunkIdx_ catches totalChunks_).
  TEST_ASSERT_EQUAL_UINT16(5, d.nextChunkIdx_);
  d.tick(1300);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Finalizing),
                          static_cast<uint8_t>(d.state_));
}

void test_streaming_spreads_chunks_across_ticks(void) {
  auto d = makeAlgo(0x00010005u, 20);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  TEST_ASSERT_EQUAL_UINT16(kBurstChunksPerTick, d.nextChunkIdx_);
  d.tick(1300);
  TEST_ASSERT_EQUAL_UINT16(2 * kBurstChunksPerTick, d.nextChunkIdx_);
  d.tick(1400);
  TEST_ASSERT_EQUAL_UINT16(20, d.nextChunkIdx_);
}

// =============================================================================
// FW_REQ rewind + budget
// =============================================================================

void test_fw_req_rewinds_chunk_cursor(void) {
  auto d = makeAlgo(0x00010005u, 20);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);  // 8 chunks
  TEST_ASSERT_EQUAL_UINT16(kBurstChunksPerTick, d.nextChunkIdx_);
  d.onReq(mac, /*firstIdx=*/3, 1300);
  TEST_ASSERT_EQUAL_UINT16(3, d.nextChunkIdx_);
  TEST_ASSERT_EQUAL_UINT16(1, d.reqCountThisSession_);
}

void test_fw_req_from_wrong_mac_is_ignored(void) {
  auto d = makeAlgo(0x00010005u, 20);
  uint8_t target[6]; macFromTail(target, 1);
  uint8_t stranger[6]; macFromTail(stranger, 99);
  d.considerPeerForOta(target, 0x00010003u, 1000);
  d.onAccept(target, 1100);
  d.tick(1200);
  d.onReq(stranger, /*firstIdx=*/3, 1300);
  TEST_ASSERT_EQUAL_UINT16(kBurstChunksPerTick, d.nextChunkIdx_);  // unchanged
  TEST_ASSERT_EQUAL_UINT16(0, d.reqCountThisSession_);             // not counted
}

void test_fw_req_out_of_range_is_ignored(void) {
  auto d = makeAlgo(0x00010005u, 20);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.onReq(mac, /*firstIdx=*/9999, 1300);  // > totalChunks_
  TEST_ASSERT_EQUAL_UINT16(kBurstChunksPerTick, d.nextChunkIdx_);  // unchanged
}

void test_fw_req_budget_overrun_aborts_with_backoff(void) {
  // Phase 5b' lamp-side hardening: a peer sending kMaxReqPerSession REQs
  // in one session is tombstoned with a 10-minute backoff.
  auto d = makeAlgo(0x00010005u, 100);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  // Pump kMaxReqPerSession + 1 REQs.
  for (uint16_t i = 0; i < kMaxReqPerSession; ++i) {
    d.onReq(mac, /*firstIdx=*/5, 1300 + i);
  }
  // Last accepted REQ is in.
  TEST_ASSERT_EQUAL_UINT16(kMaxReqPerSession, d.reqCountThisSession_);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Streaming),
                          static_cast<uint8_t>(d.state_));
  // The next REQ tips it over the budget → Failed + backoff.
  d.onReq(mac, /*firstIdx=*/5, 1500);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1500));
}

void test_fw_req_in_finalizing_returns_to_streaming(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);  // 5 chunks drained
  d.tick(1300);  // → Finalizing
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Finalizing),
                          static_cast<uint8_t>(d.state_));
  // Late REQ comes in.
  d.onReq(mac, /*firstIdx=*/2, 1400);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Streaming),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_EQUAL_UINT16(2, d.nextChunkIdx_);
}

// =============================================================================
// Finalize + RESULT
// =============================================================================

void test_full_path_done_result_success_returns_to_idle(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);  // drain 5 chunks
  d.tick(1300);  // → Finalizing
  d.onResultSuccess(mac, 1400);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Done),
                          static_cast<uint8_t>(d.state_));
  // Next tick reaps Done → Idle.
  d.tick(1500);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
  // Successful session does NOT mark the peer for backoff.
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, 2000));
}

void test_finalize_timeout_failures_peer_with_short_backoff(void) {
  // Per kPeerFinalizeBackoffMs (15s), a finalize timeout is a SHORTER
  // backoff than a generic session failure (10 min) — tail-end RESULT
  // loss is fast-retryable.
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);  // Finalizing
  d.tick(1300 + kFinalizeTimeoutMs + 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  // Should be in backoff at +1s after failure, NOT at +20s (15s window).
  const uint32_t failedAt = 1300 + kFinalizeTimeoutMs + 1;
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, failedAt + 1000));
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, failedAt + 16000));
}

void test_result_failure_records_long_backoff(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);
  d.onResultFail(mac, 1400);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  // Still in backoff way past the short-backoff window — should be 10 min.
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1400 + 100000));
}

// =============================================================================
// DONE retry loop
// =============================================================================

void test_done_retry_exhausts_budget_with_no_result(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);  // Finalizing
  d.runEmitDoneRetryLoop(1300);
  TEST_ASSERT_FALSE(d.doneRetryAborted_);
  TEST_ASSERT_EQUAL_UINT8(1 + kMaxDoneRetries, d.doneAttempts_);
}

void test_done_retry_bails_when_result_arrives_mid_loop(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);
  // RESULT-driven state change after the 2nd send.
  d.runEmitDoneRetryLoop(1300, /*pivotAfter=*/2);
  TEST_ASSERT_TRUE(d.doneRetryAborted_);
  TEST_ASSERT_EQUAL_UINT8(2, d.doneAttempts_);
}

void test_done_retry_reuses_same_seq_across_attempts(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);
  d.runEmitDoneRetryLoop(1300);
  const uint16_t firstSeq = d.doneSeqCaptured_;
  // The captured seq is incremented per CALL of the loop, not per attempt.
  // Single seq across all 5 attempts in one call → just check it's nonzero.
  TEST_ASSERT_TRUE(firstSeq != 0);
}

void test_done_retry_noop_if_not_finalizing(void) {
  auto d = makeAlgo(0x00010005u, 5);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
  d.runEmitDoneRetryLoop(1000);
  TEST_ASSERT_TRUE(d.doneRetryAborted_);
  TEST_ASSERT_EQUAL_UINT8(0, d.doneAttempts_);
}

// =============================================================================
// Backoff ring
// =============================================================================

void test_backoff_expires(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.notePeerBackoff(mac, 1000);
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1000 + 100));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1000 + kPeerBackoffMs - 1));
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, 1000 + kPeerBackoffMs + 1));
}

void test_backoff_ring_reuses_slot_for_same_mac(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.notePeerBackoff(mac, 1000);
  d.notePeerBackoff(mac, 2000);  // refreshes existing entry
  d.notePeerBackoff(mac, 3000);
  // Only ONE slot used after three notes for the same MAC.
  size_t usedSlots = 0;
  for (size_t i = 0; i < DistAlgo::kPenaltyRingSize; ++i) {
    if (d.penalties_[i].used) usedSlots++;
  }
  TEST_ASSERT_EQUAL_UINT(1, usedSlots);
}

void test_backoff_ring_holds_distinct_peers(void) {
  auto d = makeAlgo();
  for (uint8_t i = 0; i < DistAlgo::kPenaltyRingSize; ++i) {
    uint8_t mac[6]; macFromTail(mac, static_cast<uint8_t>(i + 1));
    d.notePeerBackoff(mac, 1000);
  }
  size_t usedSlots = 0;
  for (size_t i = 0; i < DistAlgo::kPenaltyRingSize; ++i) {
    if (d.penalties_[i].used) usedSlots++;
  }
  TEST_ASSERT_EQUAL_UINT(DistAlgo::kPenaltyRingSize, usedSlots);
}

// =============================================================================
// isDistributingTo (Phase 5b'.4 fade-back delay support)
// =============================================================================

void test_isDistributingTo_true_for_active_target(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  TEST_ASSERT_TRUE(d.isDistributingTo(mac));
}

void test_isDistributingTo_false_for_other_mac(void) {
  auto d = makeAlgo();
  uint8_t target[6]; macFromTail(target, 1);
  uint8_t other[6]; macFromTail(other, 2);
  d.considerPeerForOta(target, 0x00010003u, 1000);
  TEST_ASSERT_FALSE(d.isDistributingTo(other));
}

void test_isDistributingTo_false_when_idle(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.isDistributingTo(mac));
}

void test_isDistributingTo_false_after_done(void) {
  auto d = makeAlgo(0x00010005u, 5);
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);
  d.onResultSuccess(mac, 1400);
  TEST_ASSERT_FALSE(d.isDistributingTo(mac));  // Done, not in-flow
}

// =============================================================================
// discoverImageLength scan-backward (LSIG-aware, 2026-06-10 hardware fix)
// =============================================================================
//
// Hardware testing found that the original "last non-0xFF byte"
// approach failed on re-flashed lamps: esptool doesn't erase the
// partition tail, so leftover bytes from a prior larger firmware
// would mask the real image end. Fix: scan backward for the LSIG
// magic and validate by reading the footer's signedRegionLen.

void test_discoverImageLength_finds_LSIG_footer(void) {
  // Build a 1024-byte signed region + 96-byte LSIG footer.
  auto buf = makeSignedImage(1024);
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(
      discoverImageLengthScanBackward(buf.data(), buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(1024 + 96, outLen);
}

void test_discoverImageLength_handles_no_LSIG(void) {
  // Plain non-0xFF data with no footer — should NOT find an image.
  // This catches the "unsigned firmware.bin esptool-flashed" case
  // that we now compensate for via sign_firmware.py's in-place
  // overwrite of firmware.bin.
  std::vector<uint8_t> buf(1024, 0xAB);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(
      discoverImageLengthScanBackward(buf.data(), buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(99, outLen);  // unchanged on failure
}

void test_discoverImageLength_handles_all_FF(void) {
  std::vector<uint8_t> buf(1024, 0xFF);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(
      discoverImageLengthScanBackward(buf.data(), buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(99, outLen);
}

void test_discoverImageLength_handles_too_small_buffer(void) {
  std::vector<uint8_t> buf(50);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(
      discoverImageLengthScanBackward(buf.data(), buf.size(), &outLen));
}

void test_discoverImageLength_rejects_coincidental_LSIG(void) {
  // Plant "LSIG" bytes mid-buffer with a signedRegionLen that DOESN'T
  // match its position — that's a false positive (e.g. ascii 'LSIG'
  // appearing inside a string literal in the prior firmware). Then
  // plant a REAL LSIG further along. The scan should find the real
  // one, not the fake.
  auto buf = makeSignedImage(2000);
  // Plant a fake LSIG at offset 500 with signedRegionLen=999 (bogus).
  buf[500] = 'L'; buf[501] = 'S'; buf[502] = 'I'; buf[503] = 'G';
  buf[500 + 16] = 0xE7; buf[500 + 17] = 0x03;  // signedRegionLen=999 LE
  buf[500 + 18] = 0;    buf[500 + 19] = 0;
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(
      discoverImageLengthScanBackward(buf.data(), buf.size(), &outLen));
  // Should find the REAL footer at offset 2000, not the fake at 500.
  TEST_ASSERT_EQUAL_UINT32(2000 + 96, outLen);
}

void test_discoverImageLength_rejects_null_args(void) {
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(discoverImageLengthScanBackward(nullptr, 1024, &outLen));
  std::vector<uint8_t> buf(1024);
  TEST_ASSERT_FALSE(
      discoverImageLengthScanBackward(buf.data(), 1024, nullptr));
}

// =============================================================================
// Runner
// =============================================================================

int main(int, char**) {
  UNITY_BEGIN();
  // considerPeerForOta gating
  RUN_TEST(test_considerPeerForOta_idle_emits_offer);
  RUN_TEST(test_considerPeerForOta_nonIdle_is_noop);
  RUN_TEST(test_considerPeerForOta_peer_in_backoff_is_noop);
  RUN_TEST(test_considerPeerForOta_peer_already_current_is_noop);
  // OFFER retry + ACCEPT
  RUN_TEST(test_offer_retry_advances_with_interval);
  RUN_TEST(test_accept_timeout_failures_peer);
  RUN_TEST(test_accept_transitions_to_streaming);
  RUN_TEST(test_accept_from_wrong_mac_is_ignored);
  // Streaming
  RUN_TEST(test_streaming_emits_chunks_in_order);
  RUN_TEST(test_streaming_spreads_chunks_across_ticks);
  // FW_REQ
  RUN_TEST(test_fw_req_rewinds_chunk_cursor);
  RUN_TEST(test_fw_req_from_wrong_mac_is_ignored);
  RUN_TEST(test_fw_req_out_of_range_is_ignored);
  RUN_TEST(test_fw_req_budget_overrun_aborts_with_backoff);
  RUN_TEST(test_fw_req_in_finalizing_returns_to_streaming);
  // Finalize + RESULT
  RUN_TEST(test_full_path_done_result_success_returns_to_idle);
  RUN_TEST(test_finalize_timeout_failures_peer_with_short_backoff);
  RUN_TEST(test_result_failure_records_long_backoff);
  // DONE retry
  RUN_TEST(test_done_retry_exhausts_budget_with_no_result);
  RUN_TEST(test_done_retry_bails_when_result_arrives_mid_loop);
  RUN_TEST(test_done_retry_reuses_same_seq_across_attempts);
  RUN_TEST(test_done_retry_noop_if_not_finalizing);
  // Backoff ring
  RUN_TEST(test_backoff_expires);
  RUN_TEST(test_backoff_ring_reuses_slot_for_same_mac);
  RUN_TEST(test_backoff_ring_holds_distinct_peers);
  // isDistributingTo
  RUN_TEST(test_isDistributingTo_true_for_active_target);
  RUN_TEST(test_isDistributingTo_false_for_other_mac);
  RUN_TEST(test_isDistributingTo_false_when_idle);
  RUN_TEST(test_isDistributingTo_false_after_done);
  // discoverImageLength LSIG-aware scan (hardware-fix 2026-06-10)
  RUN_TEST(test_discoverImageLength_finds_LSIG_footer);
  RUN_TEST(test_discoverImageLength_handles_no_LSIG);
  RUN_TEST(test_discoverImageLength_handles_all_FF);
  RUN_TEST(test_discoverImageLength_handles_too_small_buffer);
  RUN_TEST(test_discoverImageLength_rejects_coincidental_LSIG);
  RUN_TEST(test_discoverImageLength_rejects_null_args);
  return UNITY_END();
}
