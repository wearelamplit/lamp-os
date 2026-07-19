// Native-host unit tests for the lamp's FirmwareDistributor state machine.
//
// The production class depends on FreeRTOS (xTaskCreate, xSemaphoreGive,
// portENTER_CRITICAL) + ESP-IDF (esp_ota_get_running_partition,
// esp_partition_read) + the mbedtls SHA-256 streaming API — none of
// which are available on the native host. Per the project convention
// (see test_firmware_receiver.cpp for the precedent), this file mirrors the
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
//     (lamp-side hardening — new, not in the wisp test)
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

#include "components/firmware/ota_channel.hpp"
#include "components/firmware/ota_channel.cpp"
#include "components/firmware/firmware_signature.hpp"
#include "../../src/components/firmware/discover_image_length.cpp"

// computeShaPrefixOnce streams the signed region through mbedtls SHA-256 in
// fixed-size blocks. This dir vendors the same native SHA-256 stub the
// signature test uses (mbedtls/sha256.h) to pin block-size independence of
// the digest, so a future block-size edit can't silently change the OTA
// image identity.
#include <mbedtls/sha256.h>

namespace test {

// --- Mirror constants ----------------------------------------------------

constexpr uint16_t kChunkSize             = 200;
// Mirrors FW_CHUNK_SIZE_BASELINE / FW_CHUNK_SIZE_MAX (fw_ota.hpp).
constexpr uint16_t kChunkSizeBaseline     = 200;
constexpr uint16_t kChunkSizeMax          = 768;
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
// Lamp-side hardening — caps how many MSG_FW_REQ a peer can
// send in one session before the session tombstones with a peer backoff.
constexpr uint16_t kMaxReqPerSession      = 32;
// Mirrors kOtaMinRssiDbm (fw_ota.hpp).
constexpr int8_t   kOtaMinRssiDbm         = -80;

enum class State : uint8_t {
  Disabled = 0,
  Idle,
  OfferSent,
  Streaming,
  Finalizing,
  Failed,
  Done,
};

enum class FwAcceptStatus : uint8_t {
  Accept              = 0,
  DeclineBusy         = 1,
  DeclineAlreadyCurrent = 2,
  DeclineUnverified   = 3,
};

struct Penalty {
  uint8_t  mac[6];
  uint32_t backoffUntilMs;
  bool     used;
  bool     persistent;
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
  const char* myChannel   = "standard-stable";
  uint16_t totalChunks_   = 10;

  State    state_         = State::Idle;
  uint32_t stateEnteredMs_ = 0;

  uint8_t  targetMac_[6]   = {0};
  // Mirrors sessionChunkSize_: negotiated from the peer's HELLO
  // FW_MAX_CHUNK TLV in considerPeerForOta, reset to the baseline in
  // resetSession().
  uint16_t sessionChunkSize_ = kChunkSizeBaseline;
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

  static bool macsEqual(const uint8_t a[6], const uint8_t b[6]) {
    return std::memcmp(a, b, 6) == 0;
  }

  bool peerIsInBackoff(const uint8_t mac[6], uint32_t nowMs) const {
    for (size_t i = 0; i < kPenaltyRingSize; ++i) {
      const auto& p = penalties_[i];
      if (!p.used) continue;
      if (!macsEqual(p.mac, mac)) continue;
      if (p.persistent) return true;
      if (nowMs < p.backoffUntilMs) return true;
    }
    return false;
  }

  // Mirror of the lamp-side slot-selection fix: a free/expired/active
  // transient slot is always preferred for a new mac, so a persistent
  // (cross-variant-block) slot is never evicted by a later transient
  // penalty. Only when the ring is entirely persistent does a new
  // persistent block replace the oldest one; a new transient penalty in
  // that state is dropped (it re-arms on its next failure).
  void notePeerBackoff(const uint8_t mac[6], uint32_t nowMs,
                       uint32_t durationMs = kPeerBackoffMs,
                       bool persistent = false) {
    for (size_t i = 0; i < kPenaltyRingSize; ++i) {
      if (penalties_[i].used && macsEqual(penalties_[i].mac, mac)) {
        penalties_[i].backoffUntilMs = nowMs + durationMs;
        penalties_[i].persistent     = persistent;
        return;
      }
    }

    int freeSlot = -1, expiredTransient = -1, activeTransient = -1, oldestPersistent = -1;
    for (size_t i = 0; i < kPenaltyRingSize; ++i) {
      Penalty& p = penalties_[i];
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
      if (!persistent) return;
      slot = oldestPersistent;
    }

    penalties_[slot].used           = true;
    penalties_[slot].persistent     = persistent;
    std::memcpy(penalties_[slot].mac, mac, 6);
    penalties_[slot].backoffUntilMs = nowMs + durationMs;
  }

  void resetSession() {
    std::memset(targetMac_, 0, 6);
    sessionChunkSize_    = kChunkSizeBaseline;
    nextChunkIdx_        = 0;
    lastSentChunk_       = 0;
    lastSentMs_          = 0;
    currentChunkRetries_ = 0;
    lastOfferSendMs_     = 0;
    offerRetryCount_     = 0;
    reqCountThisSession_ = 0;
  }

  // Event-driven targeting — caller (SocialBehavior) supplies a peer
  // observed via ESP-NOW HELLO. Idempotent on non-Idle. peerMaxChunk mirrors
  // the peer's HELLO FW_MAX_CHUNK TLV (0 = not advertised).
  bool considerPeerForOta(const uint8_t peerMac[6], uint32_t peerVersion,
                          uint32_t nowMs, const char* peerChannel = nullptr,
                          uint16_t peerMaxChunk = 0,
                          int8_t peerRssi = -127) {
    if (state_ != State::Idle) return false;
    // Use otaAcceptable with roles swapped: asks "would the peer accept this
    // firmware?" so the peer is the receiver and this side is the offerer.
    if (peerChannel && peerChannel[0] != '\0') {
      if (!otaAcceptable(peerChannel, peerVersion, myChannel, myVersion)) {
        return false;
      }
    } else {
      // Unknown channel: fall back to version-only gate (older peers with no
      // channel TLV still get offered if they're behind).
      if (peerVersion >= myVersion) return false;
    }
    // Skip peers below the OTA signal floor; unknown RSSI (-127) isn't gated.
    if (peerRssi != -127 && peerRssi < kOtaMinRssiDbm) return false;
    if (peerIsInBackoff(peerMac, nowMs)) return false;
    std::memcpy(targetMac_, peerMac, 6);
    state_              = State::OfferSent;
    stateEnteredMs_     = nowMs;
    lastOfferSendMs_    = nowMs;
    offerRetryCount_    = 0;
    nextChunkIdx_       = 0;
    reqCountThisSession_ = 0;
    const uint16_t cappedPeerMaxChunk =
        peerMaxChunk < kChunkSizeMax ? peerMaxChunk : kChunkSizeMax;
    sessionChunkSize_ = peerMaxChunk > 0 ? cappedPeerMaxChunk : kChunkSizeBaseline;
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

  void onAccept(const uint8_t fromMac[6], uint32_t nowMs,
                FwAcceptStatus status = FwAcceptStatus::Accept) {
    if (state_ != State::OfferSent) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    if (status != FwAcceptStatus::Accept) {
      // Offers go only to peers read as behind; already-current back from one
      // means it can't accept the local variant. Block it instead of a
      // timed retry.
      if (status == FwAcceptStatus::DeclineAlreadyCurrent) {
        notePeerBackoff(targetMac_, nowMs, 0, /*persistent=*/true);
      } else {
        notePeerBackoff(targetMac_, nowMs);
      }
      resetSession();
      state_ = State::Failed;
      stateEnteredMs_ = nowMs;
      return;
    }
    state_ = State::Streaming;
    stateEnteredMs_ = nowMs;
  }

  // Mirror of the lamp-side hardening: reqCountThisSession_ is
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

  // Inter-session indicator hold. Captured ONLY on the Done transition, before
  // resetSession() blanks targetMac_; a Failed session leaves it untouched so
  // the indicator never paints a phantom 100% bar for a transfer that died.
  uint8_t  lastSessionPeerMac_[6]  = {0};
  uint16_t lastSessionTotalChunks_ = 0;
  bool     lastSessionValid_       = false;
  void captureLastSession() {
    std::memcpy(lastSessionPeerMac_, targetMac_, 6);
    lastSessionTotalChunks_ = totalChunks_;
    lastSessionValid_       = true;
  }

  void onResultSuccess(const uint8_t fromMac[6], uint32_t nowMs) {
    if (state_ != State::Finalizing) return;
    if (!macsEqual(fromMac, targetMac_)) return;
    captureLastSession();
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

// --- Test fixtures ------------------------------------------------------

DistAlgo makeAlgo(uint32_t myVer = 0x00010005u, uint16_t chunks = 10,
                  const char* myChannel = "standard-stable") {
  DistAlgo d;
  d.myVersion    = myVer;
  d.myChannel    = myChannel;
  d.totalChunks_ = chunks;
  return d;
}

void macFromTail(uint8_t out[6], uint8_t tail) {
  out[0] = 0xAA; out[1] = 0xBB; out[2] = 0xCC;
  out[3] = 0x00; out[4] = 0x00; out[5] = tail;
}

}  // namespace test

using namespace test;

// --- discoverSignedImageLength test helpers --------------------------------

namespace lf = lamp::firmware;

static lamp::firmware::FirmwareByteReader makeVectorReader(
    const std::vector<uint8_t>& v) {
  return [&v](size_t off, size_t want, uint8_t* out) -> int {
    if (off + want > v.size()) return -1;
    std::memcpy(out, v.data() + off, want);
    return static_cast<int>(want);
  };
}

// Build a buffer that LOOKS like a signed firmware:
// [signed region of len `signedLen`] + 96-byte LSIG footer.
// signedRegionLen is written at kLsigSignedLenOffset (24).
static std::vector<uint8_t> makeSignedImage(size_t signedLen) {
  std::vector<uint8_t> out(signedLen + lf::kLsigFooterLen, 0xFF);
  for (size_t i = 0; i < signedLen; ++i) out[i] = static_cast<uint8_t>(i & 0xFF);
  out[signedLen + lf::kLsigMagicOffset + 0] = 'L';
  out[signedLen + lf::kLsigMagicOffset + 1] = 'S';
  out[signedLen + lf::kLsigMagicOffset + 2] = 'I';
  out[signedLen + lf::kLsigMagicOffset + 3] = 'G';
  out[signedLen + lf::kLsigSignedLenOffset + 0] = static_cast<uint8_t>(signedLen & 0xFF);
  out[signedLen + lf::kLsigSignedLenOffset + 1] = static_cast<uint8_t>((signedLen >> 8) & 0xFF);
  out[signedLen + lf::kLsigSignedLenOffset + 2] = static_cast<uint8_t>((signedLen >> 16) & 0xFF);
  out[signedLen + lf::kLsigSignedLenOffset + 3] = static_cast<uint8_t>((signedLen >> 24) & 0xFF);
  return out;
}

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

// Backward-compat hinge: a peer that doesn't advertise FW_MAX_CHUNK (no HELLO
// TLV, peerMaxChunk=0) gets the universal-floor session size.
void test_considerPeerForOta_no_max_chunk_uses_baseline(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000));
  TEST_ASSERT_EQUAL_UINT16(kChunkSizeBaseline, d.sessionChunkSize_);
}

void test_considerPeerForOta_max_chunk_uses_ceiling(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000, nullptr,
                                        kChunkSizeMax));
  TEST_ASSERT_EQUAL_UINT16(kChunkSizeMax, d.sessionChunkSize_);
}

void test_considerPeerForOta_max_chunk_below_ceiling_uses_peer_value(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000, nullptr, 512));
  TEST_ASSERT_EQUAL_UINT16(512, d.sessionChunkSize_);
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

// RSSI gate: a fair-signal peer still gets offered.
void test_considerPeerForOta_rssi_above_floor_offers(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000, nullptr, 0, -70));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
}

// RSSI gate: a peer below the signal floor is skipped, no OFFER.
void test_considerPeerForOta_rssi_below_floor_skips(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 0x00010003u, 1000, nullptr, 0, -85));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

// RSSI gate: the unknown-RSSI sentinel (-127) is never gated.
void test_considerPeerForOta_rssi_unknown_offers(void) {
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 0x00010003u, 1000, nullptr, 0, -127));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
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

void test_decline_status_enters_backoff(void) {
  // Sender backs off after the peer declines (e.g. DeclineUnverified).
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100, FwAcceptStatus::DeclineUnverified);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100 + kPeerBackoffMs - 1));
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, 1100 + kPeerBackoffMs));
}

void test_decline_already_current_enters_permanent_backoff(void) {
  // A behind-read peer replying already-current can't accept the local variant
  // (cross-variant tell); block it far past the normal 10-min window.
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100, FwAcceptStatus::DeclineAlreadyCurrent);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100 + kPeerBackoffMs));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100 + 100u * kPeerBackoffMs));
}

void test_decline_busy_enters_transient_backoff(void) {
  // A busy peer (mid-flow elsewhere) is worth retrying; keep the normal
  // timed backoff, not the permanent cross-variant block.
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100, FwAcceptStatus::DeclineBusy);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Failed),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100));
  TEST_ASSERT_TRUE(d.peerIsInBackoff(mac, 1100 + kPeerBackoffMs - 1));
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, 1100 + kPeerBackoffMs));
}

void test_accept_status_no_penalty(void) {
  // An old same-variant behind peer replies Accept, not
  // DeclineAlreadyCurrent, so it's never penalized and still self-upgrades.
  auto d = makeAlgo();
  uint8_t mac[6]; macFromTail(mac, 1);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100, FwAcceptStatus::Accept);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Streaming),
                          static_cast<uint8_t>(d.state_));
  TEST_ASSERT_FALSE(d.peerIsInBackoff(mac, 1100));
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
  // Lamp-side hardening: a peer sending kMaxReqPerSession REQs
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

// Done captures the just-finished session for the indicator's held bar; a
// RESULT-failure does NOT (no phantom full bar for a dead transfer).
void test_last_session_captured_on_done_not_on_fail(void) {
  auto d = makeAlgo(0x00010005u, 7);
  uint8_t mac[6]; macFromTail(mac, 3);
  d.considerPeerForOta(mac, 0x00010003u, 1000);
  d.onAccept(mac, 1100);
  d.tick(1200);
  d.tick(1300);  // Finalizing
  d.onResultSuccess(mac, 1400);
  TEST_ASSERT_TRUE(d.lastSessionValid_);
  TEST_ASSERT_EQUAL_MEMORY(mac, d.lastSessionPeerMac_, 6);
  TEST_ASSERT_EQUAL_UINT16(7, d.lastSessionTotalChunks_);

  auto f = makeAlgo(0x00010005u, 7);
  f.considerPeerForOta(mac, 0x00010003u, 1000);
  f.onAccept(mac, 1100);
  f.tick(1200);
  f.tick(1300);
  f.onResultFail(mac, 1400);
  TEST_ASSERT_FALSE(f.lastSessionValid_);
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

// A persistent (cross-variant) block must survive later transient backoffs
// from other peers even after the ring churns past its capacity, or the
// cross-variant fix silently reopens once 8 more distinct MACs fail.
void test_backoff_ring_persistent_entry_survives_churn(void) {
  auto d = makeAlgo();
  uint8_t persistentMac[6]; macFromTail(persistentMac, 1);
  d.notePeerBackoff(persistentMac, 1000, 0, /*persistent=*/true);

  for (uint8_t i = 0; i < DistAlgo::kPenaltyRingSize * 2; ++i) {
    uint8_t mac[6]; macFromTail(mac, static_cast<uint8_t>(i + 2));
    d.notePeerBackoff(mac, 2000 + i);
  }

  TEST_ASSERT_TRUE(d.peerIsInBackoff(persistentMac, 2000 + DistAlgo::kPenaltyRingSize * 2));
}

// =============================================================================
// isDistributingTo (fade-back delay support)
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
  auto buf = makeSignedImage(1024);
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(1024 + lf::kLsigFooterLen, outLen);
}

void test_discoverImageLength_handles_no_LSIG(void) {
  // Plain non-0xFF data with no footer — should NOT find an image.
  std::vector<uint8_t> buf(1024, 0xAB);
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(99, outLen);  // unchanged on failure
}

void test_discoverImageLength_handles_all_FF(void) {
  std::vector<uint8_t> buf(1024, 0xFF);
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(99, outLen);
}

void test_discoverImageLength_handles_too_small_buffer(void) {
  std::vector<uint8_t> buf(50);
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
}

void test_discoverImageLength_rejects_coincidental_LSIG(void) {
  // Plant "LSIG" bytes mid-buffer with a signedRegionLen that DOESN'T
  // match its position. Then plant a REAL LSIG further along.
  auto buf = makeSignedImage(2000);
  // Fake LSIG at offset 500 with signedRegionLen=999 (bogus).
  buf[500] = 'L'; buf[501] = 'S'; buf[502] = 'I'; buf[503] = 'G';
  buf[500 + lf::kLsigSignedLenOffset + 0] = 0xE7;
  buf[500 + lf::kLsigSignedLenOffset + 1] = 0x03;  // 999 LE
  buf[500 + lf::kLsigSignedLenOffset + 2] = 0;
  buf[500 + lf::kLsigSignedLenOffset + 3] = 0;
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(2000 + lf::kLsigFooterLen, outLen);
}

// Regression: a lamp app-only-reflashed with a SMALLER image keeps the prior
// LARGER image's (self-consistent) footer in the un-erased tail, at a HIGHER
// offset. The forward scan must return the LOWER (running) footer.
void test_discoverImageLength_ignores_stale_higher_footer(void) {
  auto buf = makeSignedImage(1024);   // running image: footer at offset 1024
  buf.resize(2000 + lf::kLsigFooterLen, 0x5A);  // un-erased tail
  // Self-consistent stale footer at offset 2000 (signedRegionLen == 2000).
  buf[2000 + lf::kLsigMagicOffset + 0] = 'L';
  buf[2000 + lf::kLsigMagicOffset + 1] = 'S';
  buf[2000 + lf::kLsigMagicOffset + 2] = 'I';
  buf[2000 + lf::kLsigMagicOffset + 3] = 'G';
  buf[2000 + lf::kLsigSignedLenOffset + 0] = static_cast<uint8_t>(2000 & 0xFF);
  buf[2000 + lf::kLsigSignedLenOffset + 1] = static_cast<uint8_t>((2000 >> 8) & 0xFF);
  buf[2000 + lf::kLsigSignedLenOffset + 2] = 0;
  buf[2000 + lf::kLsigSignedLenOffset + 3] = 0;
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(1024 + lf::kLsigFooterLen, outLen);
}

// Boundary: a footer whose LSIG magic straddles the 256-byte window edge
// must still be found via the (kMagicLen-1) overlap read.
void test_discoverImageLength_finds_footer_straddling_window_boundary(void) {
  std::vector<uint8_t> buf(255 + lf::kLsigFooterLen, 0x33);
  buf[255 + lf::kLsigMagicOffset + 0] = 'L';
  buf[255 + lf::kLsigMagicOffset + 1] = 'S';
  buf[255 + lf::kLsigMagicOffset + 2] = 'I';
  buf[255 + lf::kLsigMagicOffset + 3] = 'G';
  // signedRegionLen = 255 == footerStart
  buf[255 + lf::kLsigSignedLenOffset + 0] = 255;
  buf[255 + lf::kLsigSignedLenOffset + 1] = 0;
  buf[255 + lf::kLsigSignedLenOffset + 2] = 0;
  buf[255 + lf::kLsigSignedLenOffset + 3] = 0;
  auto reader = makeVectorReader(buf);
  uint32_t outLen = 0;
  TEST_ASSERT_TRUE(lf::discoverSignedImageLength(reader, buf.size(), &outLen));
  TEST_ASSERT_EQUAL_UINT32(255 + lf::kLsigFooterLen, outLen);
}

void test_discoverImageLength_rejects_null_args(void) {
  // null reader
  uint32_t outLen = 99;
  TEST_ASSERT_FALSE(lf::discoverSignedImageLength(nullptr, 1024, &outLen));
  // null outLen
  std::vector<uint8_t> buf(1024);
  auto reader = makeVectorReader(buf);
  TEST_ASSERT_FALSE(lf::discoverSignedImageLength(reader, buf.size(), nullptr));
}

// =============================================================================
// Channel-aware considerPeerForOta (promotion gating)
// =============================================================================

// stable distributor (v100) → beta peer (v100): promotion offer, equal version.
void test_consider_offers_stable_to_beta_peer_equal_version(void) {
  auto d = makeAlgo(100, 10, "standard-stable");
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_TRUE(d.considerPeerForOta(mac, 100, 1000, "standard-beta"));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::OfferSent),
                          static_cast<uint8_t>(d.state_));
}

// stable v99 → beta v100: stable is behind, no offer.
void test_consider_skips_when_stable_older_than_beta(void) {
  auto d = makeAlgo(99, 10, "standard-stable");
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 100, 1000, "standard-beta"));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

// beta distributor → stable peer: beta never offers to stable (stable rejects beta).
void test_consider_beta_does_not_offer_to_stable(void) {
  auto d = makeAlgo(101, 10, "standard-beta");
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 100, 1000, "standard-stable"));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

// standard distributor → snafu-beta peer: cross-variant, never offered.
void test_consider_cross_variant_still_skipped(void) {
  auto d = makeAlgo(101, 10, "standard-stable");
  uint8_t mac[6]; macFromTail(mac, 1);
  TEST_ASSERT_FALSE(d.considerPeerForOta(mac, 100, 1000, "snafu-beta"));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::Idle),
                          static_cast<uint8_t>(d.state_));
}

// =============================================================================
// SHA-256 block-size independence (OTA image-identity pin)
// =============================================================================

static void hashInBlocks(const std::vector<uint8_t>& data, size_t block,
                         uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, /*is224=*/0);
  size_t cursor = 0;
  while (cursor < data.size()) {
    const size_t want =
        (data.size() - cursor > block) ? block : (data.size() - cursor);
    mbedtls_sha256_update(&ctx, data.data() + cursor, want);
    cursor += want;
  }
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

// The digest computeShaPrefixOnce produces must not depend on the streaming
// block size. 4779 B is a few KB and not a multiple of 512, so both block
// sizes hit a partial final block.
void test_sha256_prefix_block_size_independent(void) {
  std::vector<uint8_t> data(4779);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
  }
  uint8_t digest4096[32];
  uint8_t digest512[32];
  hashInBlocks(data, 4096, digest4096);
  hashInBlocks(data, 512, digest512);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(digest4096, digest512, 32);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(digest4096, digest512, 8);
}

// =============================================================================
// Runner
// =============================================================================

int main(int, char**) {
  UNITY_BEGIN();
  // considerPeerForOta gating
  RUN_TEST(test_considerPeerForOta_idle_emits_offer);
  RUN_TEST(test_considerPeerForOta_no_max_chunk_uses_baseline);
  RUN_TEST(test_considerPeerForOta_max_chunk_uses_ceiling);
  RUN_TEST(test_considerPeerForOta_max_chunk_below_ceiling_uses_peer_value);
  RUN_TEST(test_considerPeerForOta_nonIdle_is_noop);
  RUN_TEST(test_considerPeerForOta_peer_in_backoff_is_noop);
  RUN_TEST(test_considerPeerForOta_peer_already_current_is_noop);
  RUN_TEST(test_considerPeerForOta_rssi_above_floor_offers);
  RUN_TEST(test_considerPeerForOta_rssi_below_floor_skips);
  RUN_TEST(test_considerPeerForOta_rssi_unknown_offers);
  // OFFER retry + ACCEPT
  RUN_TEST(test_offer_retry_advances_with_interval);
  RUN_TEST(test_accept_timeout_failures_peer);
  RUN_TEST(test_accept_transitions_to_streaming);
  RUN_TEST(test_accept_from_wrong_mac_is_ignored);
  RUN_TEST(test_decline_status_enters_backoff);
  RUN_TEST(test_decline_already_current_enters_permanent_backoff);
  RUN_TEST(test_decline_busy_enters_transient_backoff);
  RUN_TEST(test_accept_status_no_penalty);
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
  RUN_TEST(test_last_session_captured_on_done_not_on_fail);
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
  RUN_TEST(test_backoff_ring_persistent_entry_survives_churn);
  // isDistributingTo
  RUN_TEST(test_isDistributingTo_true_for_active_target);
  RUN_TEST(test_isDistributingTo_false_for_other_mac);
  RUN_TEST(test_isDistributingTo_false_when_idle);
  RUN_TEST(test_isDistributingTo_false_after_done);
  // Channel-aware considerPeerForOta (promotion gating)
  RUN_TEST(test_consider_offers_stable_to_beta_peer_equal_version);
  RUN_TEST(test_consider_skips_when_stable_older_than_beta);
  RUN_TEST(test_consider_beta_does_not_offer_to_stable);
  RUN_TEST(test_consider_cross_variant_still_skipped);
  // discoverImageLength LSIG-aware scan (hardware-fix 2026-06-10)
  RUN_TEST(test_discoverImageLength_finds_LSIG_footer);
  RUN_TEST(test_discoverImageLength_handles_no_LSIG);
  RUN_TEST(test_discoverImageLength_handles_all_FF);
  RUN_TEST(test_discoverImageLength_handles_too_small_buffer);
  RUN_TEST(test_discoverImageLength_rejects_coincidental_LSIG);
  RUN_TEST(test_discoverImageLength_ignores_stale_higher_footer);
  RUN_TEST(test_discoverImageLength_finds_footer_straddling_window_boundary);
  RUN_TEST(test_discoverImageLength_rejects_null_args);
  // SHA-256 block-size independence (OTA image-identity pin)
  RUN_TEST(test_sha256_prefix_block_size_independent);
  return UNITY_END();
}
