// Native-host unit tests for the FirmwareReceiver state machine + chunk
// bitmap. The production cpp depends on ShowReceiver (Arduino-coupled)
// and ESP-IDF (esp_ota_*), so we mirror the testable surface here per the
// codebase convention (see test_personality_engine for the larger
// precedent).
//
// We mirror:
//   - State enum
//   - The bitmap helpers (resetBitmap / markChunkReceived / isBitmapFull
//     / firstMissingChunk)
//   - The onOffer / onDone control-plane decisions: channel mismatch
//     silent drop, version-not-greater decline, full-bitmap → verify
//     trigger, version mismatch detection on DONE.
//
// What we don't mirror:
//   - esp_ota_* calls (we replace with a mock that records the call
//     sequence + returns controllable rc).
//   - Actual flash writes (the bitmap-marking is the proxy for "chunk
//     was accepted" — the real esp_ota_write_with_offset is host-
//     unfriendly).
//   - The full LSIG signature verify (covered in test_firmware_signature).
//
// The mirror class lives in `namespace test`; if production code drifts,
// these tests pin the OBSERVABLE contract (transitions, REQ emissions,
// RESULT codes) — which is what the wisp ↔ lamp interop test framework
// will check at integration time anyway.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace test {

// --- Mirror constants ----------------------------------------------------

constexpr uint16_t FW_CHUNK_SIZE = 200;
constexpr size_t   FW_CHANNEL_LEN = 8;
constexpr size_t   FW_SHA256_PREFIX_LEN = 8;

enum class FwAcceptStatus : uint8_t {
  Accept                = 0,
  DeclineBusy           = 1,
  DeclineAlreadyCurrent = 2,
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
constexpr const char* kMyChannel     = "stable";
constexpr uint32_t kChunkStallReqMs  = 2000;
constexpr uint32_t kStreamingHardCapMs = 60000;

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

// --- Mock ShowReceiver ---------------------------------------------------

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

class MockShowReceiver {
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

  void begin(MockShowReceiver* receiver, MockOta* ota) {
    receiver_ = receiver;
    ota_ = ota;
    state_ = State::Idle;
    publishedHandle_ = 0;
  }

  void tick(uint32_t nowMs) {
    if (state_ == State::Idle || state_ == State::Apply) return;
    if (state_ == State::Streaming) {
      if (nowMs - streamingStartMs_ > kStreamingHardCapMs) {
        abortOta();
        sendResult(FwResultStatus::PartitionWriteFail, 0xFE);
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
    ota_->totalBytesWritten += p.len;
    markChunkReceived(p.chunkIdx);
    lastChunkMs_ = mockNow_;
  }

  // Test accessors
  State state() const { return state_; }
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
    for (size_t i = 0; i < bitmapTotalChunks_; ++i) {
      const size_t byteIdx = i / 8;
      const uint8_t bit = static_cast<uint8_t>(1u << (i % 8));
      if (byteIdx >= bitmap_.size()) return UINT16_MAX;
      if ((bitmap_[byteIdx] & bit) == 0) return static_cast<uint16_t>(i);
    }
    return UINT16_MAX;
  }
  // Mirror of the real receiver's firstMissingRunLen — window-based
  // last-missing scan over kMaxReqRunChunksTest chunks. See the
  // production comment for why this returns the window span rather
  // than the longest contiguous run.
  static constexpr uint16_t kMaxReqRunChunksTest = 64;
  uint16_t firstMissingRunLen(uint16_t firstMissing) const {
    if (firstMissing == UINT16_MAX) return 0;
    uint16_t lastMissingInWindow = firstMissing;
    const size_t windowEnd =
        static_cast<size_t>(firstMissing) + kMaxReqRunChunksTest;
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
    // Channel mismatch: silent drop. NO ACCEPT.
    char ours[FW_CHANNEL_LEN] = {0};
    size_t len = std::strlen(kMyChannel);
    if (len > FW_CHANNEL_LEN) len = FW_CHANNEL_LEN;
    std::memcpy(ours, kMyChannel, len);
    if (std::memcmp(ctrl.offer.channel, ours, FW_CHANNEL_LEN) != 0) {
      return;  // silent drop
    }
    if (ctrl.offer.chunkSize != FW_CHUNK_SIZE) {
      sendAccept(ctrl, FwAcceptStatus::DeclineBusy);
      return;
    }
    if (ctrl.offer.version <= kMyVersion) {
      sendAccept(ctrl, FwAcceptStatus::DeclineAlreadyCurrent);
      return;
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
    offerTotalChunks_ = ctrl.offer.totalChunks;
    std::memcpy(offerSha256Prefix_, ctrl.offer.sha256Prefix, FW_SHA256_PREFIX_LEN);

    size_t expectedChunks = ctrl.offer.totalChunks;
    if (expectedChunks == 0 && ctrl.offer.chunkSize > 0) {
      expectedChunks = (ctrl.offer.totalLen + ctrl.offer.chunkSize - 1) /
                       ctrl.offer.chunkSize;
    }
    resetBitmap(expectedChunks);
    streamingStartMs_ = mockNow_;
    lastChunkMs_      = mockNow_;
    lastReqMs_        = 0;
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

  MockShowReceiver* receiver_ = nullptr;
  MockOta* ota_ = nullptr;
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
  uint32_t lastReqMs_ = 0;
  uint32_t mockNow_ = 0;
  std::vector<uint8_t> bitmap_;
  size_t bitmapTotalChunks_ = 0;
};

// --- Helpers -------------------------------------------------------------

static const uint8_t kWispMac[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

static PendingFirmwareControl makeOffer(uint16_t seq, uint32_t version,
                                        uint32_t totalLen, uint16_t totalChunks,
                                        const char* channel = "stable") {
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
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // Newer version on matching channel → Accept + Streaming.
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

void test_offer_channel_mismatch_silent_drop() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 600, 3, "beta");
  fr.handleControlOnLoop(offer);

  // No ACCEPT, no RESULT emitted; state remains Idle.
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

void test_offer_version_not_greater_declined() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // version == ours → already-current decline.
  auto offer = test::makeOffer(1, test::kMyVersion, 600, 3);
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwAcceptStatus::DeclineAlreadyCurrent),
      static_cast<uint8_t>(mock.accepts[0].status));
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

void test_offer_chunk_size_mismatch_declined() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 600, 3);
  offer.offer.chunkSize = 150;  // doesn't match locked FW_CHUNK_SIZE
  fr.handleControlOnLoop(offer);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::DeclineBusy),
                          static_cast<uint8_t>(mock.accepts[0].status));
}

void test_chunks_fill_bitmap() {
  test::MockShowReceiver mock;
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

void test_done_with_gap_emits_req_no_state_change() {
  test::MockShowReceiver mock;
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
  test::MockShowReceiver mock;
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
  test::MockShowReceiver mock;
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
  // Receiver returns to Failed → Idle on next tick.
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Failed),
                    static_cast<int>(fr.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::MockOtaState::Aborted),
                          static_cast<uint8_t>(ota.state));
}

void test_done_signature_fail_emits_signature_fail() {
  test::MockShowReceiver mock;
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
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // No OFFER yet — receiver is Idle.
  fr.handleChunk(test::makeChunk(0));
  TEST_ASSERT_EQUAL_UINT32(0u, ota.totalBytesWritten);
}

void test_chunks_out_of_bounds_dropped() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);  // 2 chunks max
  fr.handleControlOnLoop(offer);
  // Chunk 5 → offset 1000, way past totalLen 400.
  test::ParsedFwChunk far{};
  far.chunkIdx = 5;
  far.offset = 1000;
  far.len = 200;
  fr.handleChunk(far);
  TEST_ASSERT_EQUAL_UINT32(0u, ota.totalBytesWritten);
}

void test_stall_watchdog_emits_req() {
  test::MockShowReceiver mock;
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
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  fr.setMockNow(0);
  auto offer = test::makeOffer(1, 0x00010001, 1000, 5);
  fr.handleControlOnLoop(offer);
  mock.clear();

  // 61s elapsed without DONE → hard cap fires.
  fr.tick(61000);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.results.size()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(test::FwResultStatus::PartitionWriteFail),
      static_cast<uint8_t>(mock.results[0].status));
  TEST_ASSERT_EQUAL_UINT8(0xFE, mock.results[0].detail);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
}

void test_reoffer_same_version_idempotent_accept() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer);

  // Re-OFFER for the SAME version while streaming → idempotent re-ACCEPT.
  mock.clear();
  fr.handleControlOnLoop(offer);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(mock.accepts[0].status));
}

void test_offer_busy_while_streaming_different_version() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  auto offer1 = test::makeOffer(1, 0x00010001, 400, 2);
  fr.handleControlOnLoop(offer1);
  mock.clear();

  // Different version while streaming → DeclineBusy.
  auto offer2 = test::makeOffer(2, 0x00010005, 400, 2);
  fr.handleControlOnLoop(offer2);

  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(mock.accepts.size()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(test::FwAcceptStatus::DeclineBusy),
                          static_cast<uint8_t>(mock.accepts[0].status));
}

void test_bitmap_full_at_non_multiple_of_8() {
  test::MockShowReceiver mock;
  test::MockOta ota;
  test::FirmwareReceiver fr;
  fr.begin(&mock, &ota);

  // 5 chunks → bitmap is 1 byte but only low 5 bits matter.
  auto offer = test::makeOffer(1, 0x00010001, 5 * test::FW_CHUNK_SIZE, 5);
  fr.handleControlOnLoop(offer);
  for (uint16_t i = 0; i < 5; ++i) {
    fr.handleChunk(test::makeChunk(i));
  }
  TEST_ASSERT_TRUE(fr.isBitmapFullForTest());
}

void test_bitmap_full_at_exact_multiple_of_8() {
  test::MockShowReceiver mock;
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
  test::MockShowReceiver mock;
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

  // One tick later → back to Idle.
  fr.tick(1);
  TEST_ASSERT_EQUAL(static_cast<int>(test::FirmwareReceiver::State::Idle),
                    static_cast<int>(fr.state()));
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
// is set — the optimisation degrades to "behaves like count=1".
void test_firstMissingRunLen_returns_one_for_isolated_hole() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(100);
  // Mark 0-49, skip 50, mark 51-99 → isolated hole at 50.
  for (uint16_t i = 0; i < 50; ++i) fr.markChunkReceived(i);
  for (uint16_t i = 51; i < 100; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(50, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(1, fr.firstMissingRunLenForTest(50));
}

// Returns the run length for a contiguous run — this is where the
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

// Caps at kMaxReqRunChunks so an early-stream "everything missing"
// session doesn't dwarf the distributor's forward-stream cursor.
void test_firstMissingRunLen_caps_at_kMaxReqRunChunks() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(500);
  // Mark nothing — every chunk missing. Should cap at 64 (the
  // mirrored kMaxReqRunChunksTest constant) regardless of how
  // many actual missing chunks follow.
  TEST_ASSERT_EQUAL_UINT16(0, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(64, fr.firstMissingRunLenForTest(0));
}

// Run ends at the bitmap boundary even when shorter than the cap.
void test_firstMissingRunLen_stops_at_bitmap_end() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(40);
  // Mark 0-29 → chunks 30..39 missing (10-chunk tail run, below cap).
  for (uint16_t i = 0; i < 30; ++i) fr.markChunkReceived(i);
  TEST_ASSERT_EQUAL_UINT16(30, fr.firstMissingForTest());
  TEST_ASSERT_EQUAL_UINT16(10, fr.firstMissingRunLenForTest(30));
}

// The window approach: scattered holes within a single window get
// covered by ONE REQ — the count spans first..last-missing inclusive,
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
// cap at kMaxReqRunChunks — covering the full window is the worst
// case in terms of duplicate re-stream, but it's still one REQ per
// 64 chunks instead of one per missing chunk.
void test_firstMissingRunLen_caps_when_scattered_beyond_window() {
  test::FirmwareReceiver fr;
  fr.resetBitmap(200);
  // Mark everything except 30 and 150. firstMissing = 30. The window
  // [30, 94) contains only one missing chunk (30), so count = 1.
  // The function does NOT reach chunk 150 — that's a later REQ.
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
  RUN_TEST(test_offer_channel_mismatch_silent_drop);
  RUN_TEST(test_offer_version_not_greater_declined);
  RUN_TEST(test_offer_chunk_size_mismatch_declined);
  RUN_TEST(test_chunks_fill_bitmap);
  RUN_TEST(test_done_with_gap_emits_req_no_state_change);
  RUN_TEST(test_done_with_full_bitmap_success_path);
  RUN_TEST(test_done_version_mismatch_emits_version_mismatch);
  RUN_TEST(test_done_signature_fail_emits_signature_fail);
  RUN_TEST(test_chunks_dropped_when_not_streaming);
  RUN_TEST(test_chunks_out_of_bounds_dropped);
  RUN_TEST(test_stall_watchdog_emits_req);
  RUN_TEST(test_hard_cap_aborts_streaming);
  RUN_TEST(test_reoffer_same_version_idempotent_accept);
  RUN_TEST(test_offer_busy_while_streaming_different_version);
  RUN_TEST(test_bitmap_full_at_non_multiple_of_8);
  RUN_TEST(test_bitmap_full_at_exact_multiple_of_8);
  RUN_TEST(test_failed_state_resets_on_next_tick);
  RUN_TEST(test_firstMissingRunLen_returns_zero_when_no_missing);
  RUN_TEST(test_firstMissingRunLen_returns_one_for_isolated_hole);
  RUN_TEST(test_firstMissingRunLen_returns_run_length_for_contiguous);
  RUN_TEST(test_firstMissingRunLen_caps_at_kMaxReqRunChunks);
  RUN_TEST(test_firstMissingRunLen_stops_at_bitmap_end);
  RUN_TEST(test_firstMissingRunLen_covers_scattered_holes_in_window);
  RUN_TEST(test_firstMissingRunLen_caps_when_scattered_beyond_window);
  return UNITY_END();
}
