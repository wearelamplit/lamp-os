// Native-host unit tests for the FirmwareReceiver state machine + chunk
// bitmap. The production cpp depends on MeshLink (Arduino-coupled)
// and ESP-IDF (esp_ota_*), so we mirror the testable surface here per the
// codebase convention (see test_personality_engine for the larger
// precedent).
//
// We mirror:
//   - State enum
//   - The bitmap helpers (resetBitmap / markChunkReceived / isBitmapFull
//     / firstMissingChunk)
//   - The onOffer / onDone control-plane decisions: channel mismatch
//     decline, version-not-greater decline, full-bitmap -> verify
//     trigger, version mismatch detection on DONE.
//
// What we don't mirror:
//   - esp_ota_* calls (we replace with a mock that records the call
//     sequence + returns controllable rc).
//   - Actual flash writes (the bitmap-marking is the proxy for "chunk
//     was accepted" -- the real esp_ota_write_with_offset is host-
//     unfriendly).
//   - The full LSIG signature verify (covered in test_firmware_signature).
//
// The mirror class lives in `namespace test`; if production code drifts,
// these tests pin the OBSERVABLE contract (transitions, REQ emissions,
// RESULT codes) -- which is what the wisp <-> lamp interop test framework
// will check at integration time anyway.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "components/firmware/ota_channel.hpp"
#include "components/firmware/ota_channel.cpp"
#include "components/network/protocol/fw_ota.hpp"
#include "components/firmware/firmware_signature.hpp"

// verifyFirmwareDigestSignature stub: the mirror's auth gate calls this
// to decide the signature result. Always returns true here so existing
// hasAuth=true state-machine tests accept cleanly; the decideFwOfferAuth
// unit tests below exercise the predicate logic directly.
namespace lamp { namespace firmware {
bool verifyFirmwareDigestSignature(const uint8_t* /*digest*/,
                                   const uint8_t* /*signature*/) {
  return true;
}
}}  // namespace lamp::firmware

namespace test {

// --- Mirror constants ----------------------------------------------------

constexpr uint16_t FW_CHUNK_SIZE = 200;
constexpr uint16_t FW_CHUNK_SIZE_MAX = 768;
constexpr size_t   FW_CHANNEL_LEN = 16;
constexpr size_t   FW_SHA256_PREFIX_LEN = 8;

enum class FwAcceptStatus : uint8_t {
  Accept                = 0,
  DeclineBusy           = 1,
  DeclineAlreadyCurrent = 2,
  DeclineUnverified     = 3,
};
enum class FwReqReason : uint8_t {
  Gap           = 0,
  StallWatchdog = 1,
};
enum class FwResultStatus : uint8_t {
  Success            = 0,
  SignatureFail      = 1,
  VersionMismatch    = 2,
  PartitionWriteFail = 3,
  PartitionReadFail  = 4,
  OtaBeginFail       = 5,
  OtaEndFail         = 6,
  SetBootFail        = 7,
  OfferShaMismatch   = 8,
};

constexpr uint32_t kMyVersion        = 0x00010000;  // 1.0.0
constexpr uint32_t kChunkStallReqMs    = 2000;
constexpr uint32_t kStreamingHardCapMs = 600000;  // 10 min; matches production
constexpr uint32_t kNoProgressAbortMs  = 60000;   // 1 min; matches production

// --- Mirror PendingFirmwareControl ---------------------------------------

struct PendingFirmwareControl {
  uint8_t  msgType;        // 0x40 = OFFER, 0x44 = DONE
  uint8_t  sourceMac[6];
  uint8_t  targetMac[6];
  uint16_t seq;
  struct {
    uint32_t version;
    uint32_t totalLen;
    uint16_t chunkSize;
    char     channel[FW_CHANNEL_LEN];
    uint8_t  sha256Prefix[FW_SHA256_PREFIX_LEN];
    uint16_t footerLen;
    uint16_t totalChunks;
    bool     hasAuth;
    uint8_t  digest[lamp_protocol::FW_SHA256_FULL_LEN];
    uint8_t  signature[lamp_protocol::FW_SIG_LEN];
  } offer;
  struct {
    uint32_t version;
    uint32_t totalLen;
    uint8_t  sha256Prefix[FW_SHA256_PREFIX_LEN];
    uint16_t footerLen;
  } done;
};

struct ParsedFwChunk {
  uint16_t chunkIdx;
  uint32_t offset;
  uint16_t len;
};

// --- Mock MeshLink -------------------------------------------------------

struct AcceptRecord {
  uint16_t offerSeq;
  uint32_t version;
  FwAcceptStatus status;
};
struct ReqRecord {
  uint16_t firstChunkIdx;
  uint16_t chunkCount;
  FwReqReason reason;
};
struct ResultRecord {
  FwResultStatus status;
  uint8_t detail;
};

class MockMeshLink {
 public:
  std::vector<AcceptRecord> accepts;
  std::vector<ReqRecord> reqs;
  std::vector<ResultRecord> results;
  void clear() {
    accepts.clear();
    reqs.clear();
    results.clear();
  }
};

// --- Mock esp_ota state --------------------------------------------------

enum class MockOtaState : uint8_t {
  None       = 0,  // no handle active
  Active     = 1,  // esp_ota_begin returned OK; handle published
  Ended      = 2,  // esp_ota_end called
  Aborted    = 3,
};

struct MockOta {
  MockOtaState state = MockOtaState::None;
  uint32_t totalBytesWritten = 0;
  bool beginShouldFail = false;
  bool endShouldFail = false;
  bool setBootShouldFail = false;
  bool readShouldFail = false;
  bool signatureShouldFail = false;
  void reset() { *this = MockOta{}; }
};

// --- Mirror class --------------------------------------------------------

class FirmwareReceiver {
 public:
  enum class State : uint8_t {
    Idle            = 0,
    Streaming       = 3,
    Verify          = 4,
    Apply           = 5,
    Failed          = 6,
  };

  void begin(MockMeshLink* receiver, MockOta* ota,
             const char* myChannel = "standard-stable") {
    receiver_ = receiver;
    ota_ = ota;
    myChannel_ = myChannel ? myChannel : "standard-stable";
    state_ = State::Idle;
    publishedHandle_ = 0;
  }

  void tick(uint32_t nowMs) {
    if (state_ == State::Idle) {
      if (!bitmap_.empty()) {
        std::vector<uint8_t>().swap(bitmap_);
        bitmapTotalChunks_ = 0;
      }
      return;
    }
    if (state_ == State::Apply) return;
    if (state_ == State::Streaming) {
      if (nowMs - streamingStartMs_ > kStreamingHardCapMs) {
        abortOta();
        sendResult(FwResultStatus::PartitionWriteFail, 0xFE);
        state_ = State::Idle;
        return;
      }
      const int32_t elapsedSeen =
          static_cast<int32_t>(nowMs - lastChunkSeenMs_);
      if (lastChunkSeenMs_ != 0 &&
          elapsedSeen > static_cast<int32_t>(kNoProgressAbortMs)) {
        abortOta();
        state_ = State::Idle;
        return;
      }
      if (lastChunkMs_ != 0 && (nowMs - lastChunkMs_) > kChunkStallReqMs &&
          (lastReqMs_ == 0 || (nowMs - lastReqMs_) > kChunkStallReqMs)) {
        const uint16_t missing = firstMissingChunk();
        if (missing != UINT16_MAX) {
          sendReq(missing, 1, FwReqReason::StallWatchdog);
          lastReqMs_ = nowMs;
        }
      }
      return;
    }
    if (state_ == State::Failed) {
      state_ = State::Idle;
      return;
    }
  }

  void handleControlOnLoop(const PendingFirmwareControl& ctrl) {
    if (ctrl.msgType == 0x40) {
      onOffer(ctrl);
    } else if (ctrl.msgType == 0x44) {
      onDone(ctrl);
    }
  }

  void handleChunk(const ParsedFwChunk& p) {
    if (publishedHandle_ == 0) return;
    if (static_cast<uint32_t>(p.offset) + p.len > offerTotalLen_) return;
    if (p.len > offerChunkSize_) return;
    if (p.offset != static_cast<uint32_t>(p.chunkIdx) * offerChunkSize_) return;
    ota_->totalBytesWritten += p.len;
    markChunkReceived(p.chunkIdx);
    lastChunkMs_     = mockNow_;
    lastChunkSeenMs_ = mockNow_;
  }

  // Test accessors
  State state() const { return state_; }
  uint16_t totalChunksForTest() const { return offerTotalChunks_; }
  size_t bitmapBytesForTest() const { return bitmap_.size(); }
  bool isBitmapFullForTest() const { return isBitmapFull(); }
  uint16_t firstMissingForTest() const { return firstMissingChunk(); }
  uint16_t firstMissingRunLenForTest(uint16_t firstMissing) const {
    return firstMissingRunLen(firstMissing);
  }
  void setMockNow(uint32_t now) { mockNow_ = now; }

  // Test helpers
  void resetBitmap(size_t totalChunks) {
    bitmapTotalChunks_ = totalChunks;
    bitmap_.assign((totalChunks + 7) / 8, 0);
  }
  void markChunkReceived(uint16_t chunkIdx) {
    const size_t byteIdx = chunkIdx / 8;
    if (byteIdx >= bitmap_.size()) return;
    bitmap_[byteIdx] |= static_cast<uint8_t>(1u << (chunkIdx % 8));
  }
  bool isBitmapFull() const {
    if (bitmapTotalChunks_ == 0) return false;
    const size_t fullBytes = bitmapTotalChunks_ / 8;
    for (size_t i = 0; i < fullBytes; ++i) {
      if (bitmap_[i] != 0xFF) return false;
    }
    const size_t tailBits = bitmapTotalChunks_ % 8;
    if (tailBits == 0) return true;
    const uint8_t tailMask = static_cast<uint8_t>((1u << tailBits) - 1u);
    return fullBytes < bitmap_.size() && (bitmap_[fullBytes] & tailMask) == tailMask;
  }
  uint16_t firstMissingChunk() const {
    const size_t fullBytes = bitmapTotalChunks_ / 8;
    const size_t tailBits = bitmapTotalChunks_ % 8;
    const size_t scanBytes = fullBytes + (tailBits != 0 ? 1u : 0u);
    for (size_t b = 0; b < scanBytes && b < bitmap_.size(); ++b) {
      const uint8_t validMask =
          (b == fullBytes) ? static_cast<uint8_t>((1u << tailBits) - 1u) : 0xFFu;
      const uint8_t missing = static_cast<uint8_t>(~bitmap_[b] & validMask);
      if (missing == 0) continue;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        if (missing & (1u << bit)) return static_cast<uint16_t>(b * 8 + bit);
      }
    }
    return UINT16_MAX;
  }
  // Mirror of the real receiver's firstMissingRunLen -- window-based
  // last-missing scan over FW_MAX_REQ_RUN_CHUNKS. See the
  // production comment for why this returns the window span rather
  // than the longest contiguous run.
  uint16_t firstMissingRunLen(uint16_t firstMissing) const {
    if (firstMissing == UINT16_MAX) return 0;
    uint16_t lastMissingInWindow = firstMissing;
    const size_t windowEnd =
        static_cast<size_t>(firstMissing) + lamp_protocol::FW_MAX_REQ_RUN_CHUNKS;
    for (size_t i = firstMissing;
         i < bitmapTotalChunks_ && i < windowEnd; ++i) {
      const size_t byteIdx = i / 8;
      const uint8_t bit = static_cast<uint8_t>(1u << (i % 8));
      if (byteIdx >= bitmap_.size()) break;
      if ((bitmap_[byteIdx] & bit) == 0) {
        lastMissingInWindow = static_cast<uint16_t>(i);
      }
    }
    return static_cast<uint16_t>(lastMissingInWindow - firstMissing + 1);
  }

 private:
  void onOffer(const PendingFirmwareControl& ctrl) {
    // Null-terminate the offer channel field for otaAcceptable.
    char offerCh[FW_CHANNEL_LEN + 1] = {0};
    std::memcpy(offerCh, ctrl.offer.channel, FW_CHANNEL_LEN);

    if (ctrl.offer.chunkSize == 0 || ctrl.offer.chunkSize > FW_CHUNK_SIZE_MAX) {
      sendAccept(ctrl, FwAcceptStatus::DeclineBusy);
      return;
    }
    if (!otaAcceptable(myChannel_, kMyVersion,
                       offerCh, ctrl.offer.version)) {
      sendAccept(ctrl, FwAcceptStatus::DeclineAlreadyCurrent);
      return;
    }
    // Auth gate matches production's firmware-path order: after chunkSize +
    // channel, before erase/arm. verifyFirmwareDigestSignature is only
    // called when hasAuth is true (short-circuit preserved).
    {
      const lamp_protocol::FwAcceptStatus authStatus =
          lamp_protocol::decideFwOfferAuth(
              ctrl.offer.hasAuth,
              ctrl.offer.hasAuth &&
                  lamp::firmware::verifyFirmwareDigestSignature(
                      ctrl.offer.digest, ctrl.offer.signature));
      if (authStatus != lamp_protocol::FwAcceptStatus::Accept) {
        sendAccept(ctrl, static_cast<FwAcceptStatus>(authStatus));
        return;
      }
    }
    if (state_ == State::Streaming || state_ == State::Verify) {
      if (ctrl.offer.version == offerVersion_ &&
          std::memcmp(ctrl.sourceMac, wispMac_, 6) == 0) {
        sendAccept(ctrl, FwAcceptStatus::Accept);
        return;
      }
      sendAccept(ctrl, FwAcceptStatus::DeclineBusy);
      return;
    }

    if (ota_->beginShouldFail) {
      sendResult(FwResultStatus::OtaBeginFail, 0);
      state_ = State::Failed;
      return;
    }
    publishedHandle_ = 1;
    ota_->state = MockOtaState::Active;

    std::memcpy(wispMac_, ctrl.sourceMac, 6);
    offerSeq_         = ctrl.seq;
    offerVersion_     = ctrl.offer.version;
    offerTotalLen_    = ctrl.offer.totalLen;
    offerChunkSize_   = ctrl.offer.chunkSize;
    std::memcpy(offerSha256Prefix_, ctrl.offer.sha256Prefix, FW_SHA256_PREFIX_LEN);

    const size_t expectedChunks =
        (static_cast<size_t>(ctrl.offer.totalLen) + ctrl.offer.chunkSize - 1) /
        ctrl.offer.chunkSize;
    offerTotalChunks_ = static_cast<uint16_t>(expectedChunks);
    resetBitmap(expectedChunks);
    streamingStartMs_  = mockNow_;
    lastChunkMs_       = mockNow_;
    lastChunkSeenMs_   = mockNow_;
    lastReqMs_         = 0;
    state_ = State::Streaming;

    sendAccept(ctrl, FwAcceptStatus::Accept);
  }

  void onDone(const PendingFirmwareControl& ctrl) {
    if (state_ != State::Streaming) return;
    if (ctrl.done.version != offerVersion_) {
      abortOta();
      sendResult(FwResultStatus::VersionMismatch, 0);
      state_ = State::Failed;
      return;
    }
    if (!isBitmapFull()) {
      const uint16_t missing = firstMissingChunk();
      if (missing != UINT16_MAX) {
        sendReq(missing, 1, FwReqReason::Gap);
      }
      return;
    }
    state_ = State::Verify;
    // verifyAndApply mock
    if (ota_->endShouldFail) {
      sendResult(FwResultStatus::OtaEndFail, 0);
      state_ = State::Failed;
      return;
    }
    if (ota_->readShouldFail) {
      sendResult(FwResultStatus::PartitionReadFail, 0);
      state_ = State::Failed;
      return;
    }
    if (ota_->signatureShouldFail) {
      sendResult(FwResultStatus::SignatureFail, 0);
      state_ = State::Failed;
      return;
    }
    if (ota_->setBootShouldFail) {
      sendResult(FwResultStatus::SetBootFail, 0);
      state_ = State::Failed;
      return;
    }
    state_ = State::Apply;
    publishedHandle_ = 0;
    ota_->state = MockOtaState::Ended;
    sendResult(FwResultStatus::Success, 0);
  }

  void abortOta() {
    publishedHandle_ = 0;
    if (ota_) ota_->state = MockOtaState::Aborted;
  }

  void sendAccept(const PendingFirmwareControl& ctrl, FwAcceptStatus status) {
    if (!receiver_) return;
    receiver_->accepts.push_back({ctrl.seq, ctrl.offer.version, status});
  }
  void sendReq(uint16_t firstChunkIdx, uint16_t chunkCount, FwReqReason reason) {
    if (!receiver_) return;
    receiver_->reqs.push_back({firstChunkIdx, chunkCount, reason});
  }
  void sendResult(FwResultStatus status, uint8_t detail) {
    if (!receiver_) return;
    receiver_->results.push_back({status, detail});
  }

  MockMeshLink* receiver_ = nullptr;
  MockOta* ota_ = nullptr;
  const char* myChannel_ = "standard-stable";
  State state_ = State::Idle;
  uint32_t publishedHandle_ = 0;
  uint8_t  wispMac_[6] = {0};
  uint16_t offerSeq_ = 0;
  uint32_t offerVersion_ = 0;
  uint32_t offerTotalLen_ = 0;
  uint16_t offerChunkSize_ = 0;
  uint16_t offerTotalChunks_ = 0;
  uint8_t  offerSha256Prefix_[FW_SHA256_PREFIX_LEN] = {0};
  uint32_t streamingStartMs_ = 0;
  uint32_t lastChunkMs_ = 0;
  uint32_t lastChunkSeenMs_ = 0;
  uint32_t lastReqMs_ = 0;
  uint32_t mockNow_ = 0;
  std::vector<uint8_t> bitmap_;
  size_t bitmapTotalChunks_ = 0;
};

// --- Helpers -------------------------------------------------------------

static const uint8_t kWispMac[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

static PendingFirmwareControl makeOffer(uint16_t seq, uint32_t version,
                                        uint32_t totalLen, uint16_t totalChunks,
                                        const char* channel = "standard-stable",
                                        bool hasAuth = true) {
  PendingFirmwareControl c{};
  c.msgType = 0x40;
  c.seq = seq;
  std::memcpy(c.sourceMac, kWispMac, 6);
  c.offer.version = version;
  c.offer.totalLen = totalLen;
  c.offer.chunkSize = FW_CHUNK_SIZE;
  c.offer.totalChunks = totalChunks;
  c.offer.footerLen = 96;
  std::memset(c.offer.channel, 0, FW_CHANNEL_LEN);
  size_t len = std::strlen(channel);
  if (len > FW_CHANNEL_LEN) len = FW_CHANNEL_LEN;
  std::memcpy(c.offer.channel, channel, len);
  // hasAuth=true; digest/sig zeroed — the test shim always returns valid.
  c.offer.hasAuth = hasAuth;
  std::memset(c.offer.digest, 0, sizeof(c.offer.digest));
  std::memset(c.offer.signature, 0, sizeof(c.offer.signature));
  return c;
}

static PendingFirmwareControl makeDone(uint32_t version, uint32_t totalLen) {
  PendingFirmwareControl c{};
  c.msgType = 0x44;
  c.seq = 0xBEEF;
  std::memcpy(c.sourceMac, kWispMac, 6);
  c.done.version = version;
  c.done.totalLen = totalLen;
  c.done.footerLen = 96;
  return c;
}

static ParsedFwChunk makeChunk(uint16_t idx, uint16_t len = FW_CHUNK_SIZE) {
  ParsedFwChunk p{};
  p.chunkIdx = idx;
  p.offset = static_cast<uint32_t>(idx) * FW_CHUNK_SIZE;
  p.len = len;
  return p;
}

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

// --- Tests --------------------------------------------------------------

void test_offer_accept_transitions_to_streaming() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // Newer version on matching channel -> Accept + Streaming.
  auto offer = test::makeOffer(/*seq=*/1, /*version=*/0x00010001,
                                /*totalLen=*/600, /*totalChunks=*/3);
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL_UINT16(1, mock.accepts[0].offerSeq);
  TEST_ASSERT_EQUAL_UINT32(0x00010001u, mock.accepts[0].version);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Streaming),
                    static_cast<int>(fr.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::MockOtaState::Active),
                          static_cast<uint8_t>(ota.state));
}

// A mutated totalChunks (authentic-looking OFFER, wire totalChunks inflated)
// must not size the bitmap: the count is derived from totalLen, so filling the
// real chunks still completes the transfer instead of stalling on phantom bits.
void test_offer_forged_totalChunks_bounded_by_totalLen() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // 600 B / 200 = 3 real chunks, but the wire claims 0xFFFF.
  auto offer = test::makeOffer(1, 0x00010001, 600, 0xFFFF);
  fr.handleControlOnLoop(offer);
  TEST_ASSERT_EQUAL_UINT16(3, fr.totalChunksForTest());

  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  fr.handleChunk(test::makeChunk(2));
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

// Cross-channel offer is now DeclineAlreadyCurrent (not silent drop); the
// distributor's upstream filter is the primary gate against unwanted offers.
void test_offer_channel_mismatch_declined() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // standard-stable receiver, standard-beta offer at higher version.
  auto offer = test::makeOffer(1, 0x00010001, 600, 3, "standard-beta");
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

void test_offer_version_not_greater_declined() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // version == ours -> already-current decline.
  auto offer = test::makeOffer(1, test::kMyVersion, 600, 3);
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

void test_offer_chunk_size_in_range_accepted() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // A v2 distributor's larger negotiated chunk (512) is in 1..MAX -> Accept.
  auto offer = test::makeOffer(1, 0x00010001, 1200, 3);
  offer.offer.chunkSize = 512;
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(mock.accepts[0].status));
  // 1200 / 512 -> 3 chunks.
  TEST_ASSERT_EQUAL_UINT16(3, fr.totalChunksForTest());
}

void test_offer_chunk_size_out_of_range_declined() {
  for (uint16_t badSize : {static_cast<uint16_t>(0),
                           static_cast<uint16_t>(test::FW_CHUNK_SIZE_MAX + 1)}) {
    test::MockMeshLink mock;
    test::MockOta ota;
    test::FirmwareReceiver fr;
    fr.begin(&mock, &ota);

    auto offer = test::makeOffer(1, 0x00010001, 600, 3);
    offer.offer.chunkSize = badSize;
    fr.handleControlOnLoop(offer);

    TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(test::FwAcceptStatus::DeclineBusy),
        static_cast<uint8_t>(mock.accepts[0].status));
  }
}

// offset == chunkIdx * chunkSize is enforced by the receiver, the only party
// holding the session's negotiated chunkSize. A misaligned chunk is dropped;
// an aligned one fills the bitmap.
void test_chunk_offset_alignment_enforced_by_receiver() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 600, 3);  // baseline 200
  fr.handleControlOnLoop(offer);

  // Misaligned: chunkIdx=1 but offset != 1*200. Dropped.
  test::ParsedFwChunk bad = test::makeChunk(1);
  bad.offset = 199;
  fr.handleChunk(bad);
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingForTest());

  // Aligned baseline-200 sender: offset == idx*200. Accepted.
  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  fr.handleChunk(test::makeChunk(2));
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

// A short last chunk (len < chunkSize) still aligns on chunkIdx * chunkSize,
// not chunkIdx * len.
void test_chunk_short_last_chunk_offset_alignment() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // 450 B / 200 -> 3 chunks, last one short (50 B).
  auto offer = test::makeOffer(1, 0x00010001, 450, 3);
  fr.handleControlOnLoop(offer);

  // Misaligned: offset computed as chunkIdx * len (100) instead of
  // chunkIdx * chunkSize (400). Dropped.
  test::ParsedFwChunk bad = test::makeChunk(2, 50);
  bad.offset = 2u * 50;
  fr.handleChunk(bad);
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingForTest());

  // Aligned: offset == 2*200 despite the 50 B payload. Accepted.
  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  fr.handleChunk(test::makeChunk(2, 50));
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

void test_chunks_fill_bitmap() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // 3 chunks of 200 bytes each = totalLen 600.
  auto offer = test::makeOffer(1, 0x00010001, 600, 3);
  fr.handleControlOnLoop(offer);
  TEST_ASSERT_FALSE(fr.isBitmapFullForTest());
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingForTest());

  // Receive chunk 0.
  fr.handleChunk(test::makeChunk(0));
  TEST_ASSERT_FALSE(fr.isBitmapFullForTest());
  TEST_ASSERT_EQUAL_UINT16(1, fr.firstMissingForTest());

  // Receive chunk 2 out-of-order; chunk 1 still missing.
  fr.handleChunk(test::makeChunk(2));
  TEST_ASSERT_EQUAL_UINT16(1, fr.firstMissingForTest());
  TEST_ASSERT_FALSE(fr.isBitmapFullForTest());

  // Receive chunk 1.
  fr.handleChunk(test::makeChunk(1));
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT32(600u, ota.totalBytesWritten);
}

void test_first_missing_in_partial_tail_byte() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(20);  // fullBytes=2, tailBits=4
  for (uint16_t i = 0; i < 20; ++i) fr.markChunkReceived(i);
  // Padding bits above chunk 19 must not read as missing.
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, fr.firstMissingForTest());

  fr.resetBitmap(20);
  for (uint16_t i = 0; i < 20; ++i) {
    if (i != 17) fr.markChunkReceived(i);
  }
  TEST_ASSERT_EQUAL_UINT16(17, fr.firstMissingForTest());
}

void test_done_with_gap_emits_req_no_state_change() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 600, 3);
  fr.handleControlOnLoop(offer);
  mock.clear();

  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(2));
  // Chunk 1 still missing.
  auto done = test::makeDone(0x00010001, 600);
  fr.handleControlOnLoop(done);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.reqs.size()));
  TEST_ASSERT_EQUAL_UINT16(1, mock.reqs[0].firstChunkIdx);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwReqReason::Gap),
                          static_cast<uint8_t>(mock.reqs[0].reason));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Streaming),
                    static_cast<int>(fr.state()));
}

void test_done_with_full_bitmap_success_path() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);
  mock.clear();

  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());

  auto done = test::makeDone(0x00010001, 400);
  fr.handleControlOnLoop(done);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwResultStatus::Success),
                          static_cast<uint8_t>(mock.results[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Apply),
                    static_cast<int>(fr.state()));
}

void test_done_version_mismatch_emits_version_mismatch() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);
  mock.clear();
  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));

  // DONE arrives with a different version.
  auto done = test::makeDone(0x00010002, 400);
  fr.handleControlOnLoop(done);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwResultStatus::VersionMismatch),
      static_cast<uint8_t>(mock.results[0].status));
  // Receiver returns to Failed -> Idle on next tick.
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Failed),
                    static_cast<int>(fr.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::MockOtaState::Aborted),
                          static_cast<uint8_t>(ota.state));
}

void test_done_signature_fail_emits_signature_fail() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);
  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  mock.clear();

  ota.signatureShouldFail = true;
  auto done = test::makeDone(0x00010001, 400);
  fr.handleControlOnLoop(done);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwResultStatus::SignatureFail),
      static_cast<uint8_t>(mock.results[0].status));
}

void test_chunks_dropped_when_not_streaming() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // No OFFER yet -- receiver is Idle.
  fr.handleChunk(test::makeChunk(0));
  TEST_ASSERT_EQUAL_UINT32(0u, ota.totalBytesWritten);
}

void test_chunks_out_of_bounds_dropped() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);  // 2 chunks max
  fr.handleControlOnLoop(offer);
  // Chunk 5 -> offset 1000, way past totalLen 400.
  test::ParsedFwChunk far{};
  far.chunkIdx = 5;
  far.offset = 1000;
  far.len = 200;
  fr.handleChunk(far);
  TEST_ASSERT_EQUAL_UINT32(0u, ota.totalBytesWritten);
}

void test_stall_watchdog_emits_req() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  fr.setMockNow(0);
  auto offer = test::makeOffer(1, 0x00010001, 1000, 5);  // 5 chunks
  fr.handleControlOnLoop(offer);
  mock.clear();

  fr.setMockNow(100);
  fr.handleChunk(test::makeChunk(0));

  // Advance 2.5s without further chunks; tick should emit REQ for chunk 1.
  fr.setMockNow(2600);
  fr.tick(2600);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.reqs.size()));
  TEST_ASSERT_EQUAL_UINT16(1, mock.reqs[0].firstChunkIdx);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwReqReason::StallWatchdog),
                          static_cast<uint8_t>(mock.reqs[0].reason));

  // Immediate re-tick: rate-limited, no new REQ.
  fr.tick(2700);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.reqs.size()));
}

void test_hard_cap_aborts_streaming() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  fr.setMockNow(0);
  auto offer = test::makeOffer(1, 0x00010001, 1000, 5);
  fr.handleControlOnLoop(offer);
  mock.clear();

  // Simulate chunks arriving just often enough to keep the no-progress
  // abort at bay (every 30s), but never completing the transfer so the
  // 10-min hard cap fires. At 601s we're past kStreamingHardCapMs=600000.
  for (uint32_t t = 30000; t < 601000; t += 30000) {
    fr.setMockNow(t);
    // A chunk keeps lastChunkSeenMs_ fresh so only the hard cap fires.
    if (t < 600000) {
      fr.handleChunk(test::makeChunk(0));  // dup writes OK; bitmap already set
    }
    fr.tick(t);
  }
  fr.setMockNow(601000);
  fr.tick(601000);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwResultStatus::PartitionWriteFail),
      static_cast<uint8_t>(mock.results[0].status));
  TEST_ASSERT_EQUAL_UINT8(0xFE, mock.results[0].detail);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

// After a session aborts back to Idle, the next Idle tick reclaims the bitmap
// capacity rather than pinning it until reboot.
void test_idle_tick_frees_bitmap_after_abort() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  fr.setMockNow(100);
  auto offer = test::makeOffer(1, 0x00010001, 1000, 5);
  fr.handleControlOnLoop(offer);
  fr.handleChunk(test::makeChunk(0));  // seeds lastChunkSeenMs_ = 100
  TEST_ASSERT_TRUE(fr.bitmapBytesForTest() > 0u);

  // No chunk for > kNoProgressAbortMs → abort to Idle.
  fr.setMockNow(100 + test::kNoProgressAbortMs + 1);
  fr.tick(100 + test::kNoProgressAbortMs + 1);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));

  // Next Idle tick drops the capacity.
  fr.tick(100 + test::kNoProgressAbortMs + 2);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(fr.bitmapBytesForTest()));
}

void test_reoffer_same_version_idempotent_accept() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);

  // Re-OFFER for the SAME version while streaming -> idempotent re-ACCEPT.
  mock.clear();
  fr.handleControlOnLoop(offer);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(mock.accepts[0].status));
}

void test_offer_busy_while_streaming_different_version() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer1 = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer1);
  mock.clear();

  // Different version while streaming -> DeclineBusy.
  auto offer2 = test::makeOffer(2, 0x00010005, 400, 2);
  fr.handleControlOnLoop(offer2);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::DeclineBusy),
                          static_cast<uint8_t>(mock.accepts[0].status));
}

void test_bitmap_full_at_non_multiple_of_8() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // 5 chunks -> bitmap is 1 byte but only low 5 bits matter.
  auto offer = test::makeOffer(1, 0x00010001, 5 * test::FW_CHUNK_SIZE, 5);
  fr.handleControlOnLoop(offer);
  for (uint16_t i = 0; i < 5; ++i) {
    fr.handleChunk(test::makeChunk(i));
  }
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

void test_bitmap_full_at_exact_multiple_of_8() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 8 * test::FW_CHUNK_SIZE, 8);
  fr.handleControlOnLoop(offer);
  for (uint16_t i = 0; i < 8; ++i) {
    fr.handleChunk(test::makeChunk(i));
  }
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

void test_failed_state_resets_on_next_tick() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);
  fr.handleChunk(test::makeChunk(0));
  fr.handleChunk(test::makeChunk(1));
  ota.signatureShouldFail = true;
  fr.handleControlOnLoop(test::makeDone(0x00010001, 400));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Failed),
                    static_cast<int>(fr.state()));

  // One tick later -> back to Idle.
  fr.tick(1);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

// --- Promotion tests (Task 2) -------------------------------------------

// beta receiver accepts stable offer at the same version (graduation).
void test_offer_promotion_equal_version_accepted() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota, "standard-beta");

  auto offer = test::makeOffer(1, test::kMyVersion, 400, 2, "standard-stable");
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Streaming),
                    static_cast<int>(fr.state()));
}

// beta receiver rejects stable offer when stable version is older.
void test_offer_promotion_stable_older_rejected() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota, "standard-beta");

  // Our beta version is kMyVersion; stable offers kMyVersion - 1 (older).
  auto offer = test::makeOffer(1, test::kMyVersion - 1, 400, 2, "standard-stable");
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

// stable receiver rejects a beta offer even if the beta version is newer.
void test_offer_stable_receives_beta_rejected() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota, "standard-stable");

  auto offer = test::makeOffer(1, 0x00010001, 400, 2, "standard-beta");
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

// Cross-variant offer is always DeclineAlreadyCurrent regardless of version.
void test_offer_cross_variant_rejected() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota, "snafu-beta");

  auto offer = test::makeOffer(1, 0x00010001, 400, 2, "standard-beta");
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

// --- decideFwOfferAuth unit tests ----------------------------------------

void test_decideFwOfferAuth_no_auth_declines() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lamp_protocol::FwAcceptStatus::DeclineUnverified),
      static_cast<uint8_t>(lamp_protocol::decideFwOfferAuth(false, false)));
}

void test_decideFwOfferAuth_auth_but_invalid_sig_declines() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lamp_protocol::FwAcceptStatus::DeclineUnverified),
      static_cast<uint8_t>(lamp_protocol::decideFwOfferAuth(true, false)));
}

void test_decideFwOfferAuth_auth_and_valid_sig_accepts() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lamp_protocol::FwAcceptStatus::Accept),
      static_cast<uint8_t>(lamp_protocol::decideFwOfferAuth(true, true)));
}

// Full-session: hasAuth=false → DeclineUnverified, no erase/arm.
void test_offer_no_auth_declined_no_stream() {
  test::MockMeshLink mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2,
                               "standard-stable", /*hasAuth=*/false);
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineUnverified),
      static_cast<uint8_t>(mock.accepts[0].status));
  // State stays Idle — no erase or arm occurred.
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::MockOtaState::None),
                          static_cast<uint8_t>(ota.state));
}

// --- firstMissingRunLen ---

// Returns 0 when there is no missing chunk (sentinel UINT16_MAX in).
void test_firstMissingRunLen_returns_zero_when_no_missing() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(100);
  // Mark every chunk as received.
  for (uint16_t i = 0; i < 100; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingRunLenForTest(UINT16_MAX));
}

// Returns 1 when only the first-missing chunk is unset and the next
// is set -- the optimisation degrades to "behaves like count=1".
void test_firstMissingRunLen_returns_one_for_isolated_hole() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(100);
  // Mark 0-49, skip 50, mark 51-99 -> isolated hole at 50.
  for (uint16_t i = 0; i < 50; ++i) fr.markChunkReceived(i);
  for (uint16_t i = 51; i < 100; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(50, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(1, fr.firstMissingRunLenForTest(50));
}

// Returns the run length for a contiguous run -- this is where the
// optimisation pays for itself versus count=1.
void test_firstMissingRunLen_returns_run_length_for_contiguous() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(100);
  // Mark 0-29, skip 30..49 (20-chunk run), mark 50-99.
  for (uint16_t i = 0; i < 30; ++i) fr.markChunkReceived(i);
  for (uint16_t i = 50; i < 100; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(30, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(20, fr.firstMissingRunLenForTest(30));
}

// Caps at FW_MAX_REQ_RUN_CHUNKS so an early-stream "everything missing"
// session doesn't dwarf the distributor's forward-stream cursor.
void test_firstMissingRunLen_caps_at_kMaxReqRunChunks() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(500);
  // Mark nothing -- every chunk missing. Should cap at FW_MAX_REQ_RUN_CHUNKS
  // regardless of how many actual missing chunks follow.
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(lamp_protocol::FW_MAX_REQ_RUN_CHUNKS,
                           fr.firstMissingRunLenForTest(0));
}

// Run ends at the bitmap boundary even when shorter than the cap.
void test_firstMissingRunLen_stops_at_bitmap_end() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(40);
  // Mark 0-29 -> chunks 30..39 missing (10-chunk tail run, below cap).
  for (uint16_t i = 0; i < 30; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(30, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(10, fr.firstMissingRunLenForTest(30));
}

// The window approach: scattered holes within a single window get
// covered by ONE REQ -- the count spans first..last-missing inclusive,
// even if some chunks in between are already received. This is the
// real-world win versus a contiguous-only scan.
void test_firstMissingRunLen_covers_scattered_holes_in_window() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(100);
  // Mark 0-29 received. Then 30 missing, 31-32 received, 33 missing,
  // 34-35 received, 36 missing, then 37-99 received.
  // Within the 64-chunk window starting at 30: last missing is 36,
  // so count = 36 - 30 + 1 = 7.
  for (uint16_t i = 0; i < 30; ++i) fr.markChunkReceived(i);
  fr.markChunkReceived(31);
  fr.markChunkReceived(32);
  fr.markChunkReceived(34);
  fr.markChunkReceived(35);
  for (uint16_t i = 37; i < 100; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(30, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(7, fr.firstMissingRunLenForTest(30));
}

// If the only missing chunk is far away inside the window, we still
// cap at kMaxReqRunChunks -- covering the full window is the worst
// case in terms of duplicate re-stream, but it's still one REQ per
// 64 chunks instead of one per missing chunk.
void test_firstMissingRunLen_caps_when_scattered_beyond_window() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(200);
  // Mark everything except 30 and 150. firstMissing = 30. The window
  // [30, 94) contains only one missing chunk (30), so count = 1.
  // The function does NOT reach chunk 150 -- that's a later REQ.
  for (uint16_t i = 0; i < 200; ++i) fr.markChunkReceived(i);
  // Now selectively unset 30 and 150.
  // (test::FirmwareReceiver has no unset; rebuild the bitmap by hand.)
  fr.resetBitmap(200);
  for (uint16_t i = 0; i < 200; ++i) {
    if (i == 30 || i == 150) continue;
    fr.markChunkReceived(i);
  }
  TEST_ASSERT_EQUAL_UINT16(30, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(1, fr.firstMissingRunLenForTest(30));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_offer_accept_transitions_to_streaming);
  RUN_TEST(test_offer_forged_totalChunks_bounded_by_totalLen);
  RUN_TEST(test_offer_channel_mismatch_declined);
  RUN_TEST(test_offer_version_not_greater_declined);
  RUN_TEST(test_offer_chunk_size_in_range_accepted);
  RUN_TEST(test_offer_chunk_size_out_of_range_declined);
  RUN_TEST(test_chunk_offset_alignment_enforced_by_receiver);
  RUN_TEST(test_chunk_short_last_chunk_offset_alignment);
  RUN_TEST(test_chunks_fill_bitmap);
  RUN_TEST(test_first_missing_in_partial_tail_byte);
  RUN_TEST(test_done_with_gap_emits_req_no_state_change);
  RUN_TEST(test_done_with_full_bitmap_success_path);
  RUN_TEST(test_done_version_mismatch_emits_version_mismatch);
  RUN_TEST(test_done_signature_fail_emits_signature_fail);
  RUN_TEST(test_chunks_dropped_when_not_streaming);
  RUN_TEST(test_chunks_out_of_bounds_dropped);
  RUN_TEST(test_stall_watchdog_emits_req);
  RUN_TEST(test_hard_cap_aborts_streaming);
  RUN_TEST(test_idle_tick_frees_bitmap_after_abort);
  RUN_TEST(test_reoffer_same_version_idempotent_accept);
  RUN_TEST(test_offer_busy_while_streaming_different_version);
  RUN_TEST(test_bitmap_full_at_non_multiple_of_8);
  RUN_TEST(test_bitmap_full_at_exact_multiple_of_8);
  RUN_TEST(test_failed_state_resets_on_next_tick);
  RUN_TEST(test_offer_promotion_equal_version_accepted);
  RUN_TEST(test_offer_promotion_stable_older_rejected);
  RUN_TEST(test_offer_stable_receives_beta_rejected);
  RUN_TEST(test_offer_cross_variant_rejected);
  RUN_TEST(test_decideFwOfferAuth_no_auth_declines);
  RUN_TEST(test_decideFwOfferAuth_auth_but_invalid_sig_declines);
  RUN_TEST(test_decideFwOfferAuth_auth_and_valid_sig_accepts);
  RUN_TEST(test_offer_no_auth_declined_no_stream);
  RUN_TEST(test_firstMissingRunLen_returns_zero_when_no_missing);
  RUN_TEST(test_firstMissingRunLen_returns_one_for_isolated_hole);
  RUN_TEST(test_firstMissingRunLen_returns_run_length_for_contiguous);
  RUN_TEST(test_firstMissingRunLen_caps_at_kMaxReqRunChunks);
  RUN_TEST(test_firstMissingRunLen_stops_at_bitmap_end);
  RUN_TEST(test_firstMissingRunLen_covers_scattered_holes_in_window);
  RUN_TEST(test_firstMissingRunLen_caps_when_scattered_beyond_window);
  return UNITY_END();
}
