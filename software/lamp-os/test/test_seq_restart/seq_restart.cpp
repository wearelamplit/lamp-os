// Native tests for a sender's seq across a reboot against receivers' dedup:
//   - SenderSeq starts each boot at a random value and only counts up from it.
//   - Lamps already in the field drop every FW_OFFER from a seed lamp whose
//     seq restarts at 0, offers addressed to other lamps included, because
//     the offer is broadcast and their dedup records it before the address
//     check and never ages it.
//   - The same field lamps take the offers once each boot starts at random.
//   - Current receivers take a restarted sender's offer once the age passes.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "components/network/protocol/lamp_protocol.hpp"

using lamp_protocol::SenderSeq;

void setUp(void) { lamp_protocol::nativeSeqEntropy() = 0; }
void tearDown(void) {}

namespace {

const uint8_t kSeed[6]  = {0x30, 0xAE, 0xA4, 0x00, 0x00, 0x01};
const uint8_t kVamp[6]  = {0xB4, 0xBF, 0xE9, 0x1A, 0xC7, 0xE4};
const uint8_t kMilky[6] = {0xD0, 0xEF, 0x76, 0x33, 0x28, 0xBC};

// The dedup of the lamps in the field: no age, so a tuple stays until 16 newer
// ones push it out.
class FieldDedupRing {
 public:
  bool record(const uint8_t mac[6], uint8_t msgType, uint16_t seq) {
    for (const Entry& e : entries_) {
      if (e.used && e.msgType == msgType && e.seq == seq &&
          std::memcmp(e.mac, mac, 6) == 0) {
        return false;
      }
    }
    Entry& slot = entries_[head_];
    slot = Entry{true, msgType, seq, {}};
    std::memcpy(slot.mac, mac, 6);
    head_ = (head_ + 1) % 16;
    return true;
  }

 private:
  struct Entry {
    bool used = false;
    uint8_t msgType = 0;
    uint16_t seq = 0;
    uint8_t mac[6] = {};
  };
  Entry entries_[16];
  size_t head_ = 0;
};

bool addressedTo(const uint8_t target[6], const uint8_t me[6]) {
  static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return std::memcmp(target, me, 6) == 0 || std::memcmp(target, bcast, 6) == 0;
}

size_t buildOffer(uint8_t* buf, size_t cap, uint16_t seq, const uint8_t target[6]) {
  const uint8_t sha[lamp_protocol::FW_SHA256_PREFIX_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
  return lamp_protocol::buildFwOffer(buf, cap, seq, kSeed, target,
                                     /*version=*/0x00010303, /*totalLen=*/904000,
                                     /*chunkSize=*/768, "standard-stable", 15,
                                     sha, /*footerLen=*/136, /*totalChunks=*/1178);
}

// A field lamp's FW_OFFER receive order: parse, dedup, then the address check.
// True when the offer reaches its reply path, which answers every offer.
struct FieldLamp {
  const uint8_t* mac;
  FieldDedupRing dedup;

  bool hears(const uint8_t* frame, size_t len) {
    lamp_protocol::ParsedFwOffer p;
    if (!lamp_protocol::parseFwOffer(frame, len, p)) return false;
    if (!dedup.record(p.sourceMac, lamp_protocol::MSG_FW_OFFER, p.seq)) return false;
    return addressedTo(p.targetMac, mac);
  }
};

struct CurrentLamp {
  const uint8_t* mac;
  lamp_protocol::DedupRing<16> dedup;

  bool hears(const uint8_t* frame, size_t len, uint32_t nowMs) {
    lamp_protocol::ParsedFwOffer p;
    if (!lamp_protocol::parseFwOffer(frame, len, p)) return false;
    if (!dedup.record(p.sourceMac, lamp_protocol::MSG_FW_OFFER, p.seq, nowMs)) {
      return false;
    }
    return addressedTo(p.targetMac, mac);
  }
};

// One seed boot: an offer round to vamp, then milky, each broadcast so both
// lamps hear both. Returns how many offers reached their target's reply path.
template <typename NextSeq>
int offerRound(FieldLamp& vamp, FieldLamp& milky, NextSeq nextSeq) {
  int accepted = 0;
  uint8_t buf[lamp_protocol::FW_OFFER_AUTH_SIZE];
  for (const uint8_t* target : {kVamp, kMilky}) {
    const size_t n = buildOffer(buf, sizeof(buf), nextSeq(), target);
    TEST_ASSERT_TRUE(n > 0);
    const bool v = vamp.hears(buf, n);
    const bool m = milky.hears(buf, n);
    accepted += (target == kVamp ? v : m) ? 1 : 0;
  }
  return accepted;
}

}  // namespace

void test_sender_seq_starts_at_drawn_value() {
  lamp_protocol::nativeSeqEntropy() = 0x5EED4321u;
  SenderSeq seq;
  TEST_ASSERT_EQUAL_UINT16(0x4321, seq.next());
  TEST_ASSERT_EQUAL_UINT16(0x4322, seq.next());
}

void test_sender_seq_draws_start_once() {
  lamp_protocol::nativeSeqEntropy() = 900;
  SenderSeq seq;
  TEST_ASSERT_EQUAL_UINT16(900, seq.next());
  lamp_protocol::nativeSeqEntropy() = 31000;
  TEST_ASSERT_EQUAL_UINT16(901, seq.next());
  TEST_ASSERT_EQUAL_UINT16(902, seq.next());
}

void test_sender_seq_wraps_through_zero() {
  lamp_protocol::nativeSeqEntropy() = 0xFFFE;
  SenderSeq seq;
  TEST_ASSERT_EQUAL_UINT16(0xFFFE, seq.next());
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, seq.next());
  TEST_ASSERT_EQUAL_UINT16(0x0000, seq.next());
}

void test_field_lamps_drop_offers_after_seed_restarts_at_zero() {
  FieldLamp vamp{kVamp, {}};
  FieldLamp milky{kMilky, {}};
  uint16_t counter = 0;
  auto fromZero = [&counter] { return counter++; };
  TEST_ASSERT_EQUAL_INT(2, offerRound(vamp, milky, fromZero));
  for (int boot = 0; boot < 5; ++boot) {
    counter = 0;
    TEST_ASSERT_EQUAL_INT(0, offerRound(vamp, milky, fromZero));
  }
}

// The overheard offer is enough: milky never heard an offer to itself at seq
// 0, yet drops one after the restart because it recorded vamp's.
void test_field_lamp_drops_seq_it_only_overheard() {
  FieldLamp vamp{kVamp, {}};
  FieldLamp milky{kMilky, {}};
  uint8_t buf[lamp_protocol::FW_OFFER_AUTH_SIZE];
  size_t n = buildOffer(buf, sizeof(buf), 0, kVamp);
  TEST_ASSERT_TRUE(vamp.hears(buf, n));
  TEST_ASSERT_FALSE(milky.hears(buf, n));
  n = buildOffer(buf, sizeof(buf), 0, kMilky);
  TEST_ASSERT_FALSE(milky.hears(buf, n));
}

void test_field_lamps_accept_offers_when_each_boot_starts_random() {
  FieldLamp vamp{kVamp, {}};
  FieldLamp milky{kMilky, {}};
  const uint32_t bootEntropy[] = {0x0000A11Cu, 0x00003B07u, 0x0000F00Du,
                                  0x000071E2u, 0x000005A9u, 0x0000C4D3u};
  for (uint32_t entropy : bootEntropy) {
    lamp_protocol::nativeSeqEntropy() = entropy;
    SenderSeq seq;
    TEST_ASSERT_EQUAL_INT(2, offerRound(vamp, milky, [&seq] { return seq.next(); }));
  }
}

void test_current_lamp_takes_restarted_offer_after_dedup_age() {
  CurrentLamp milky{kMilky, {}};
  uint8_t buf[lamp_protocol::FW_OFFER_AUTH_SIZE];
  const size_t n = buildOffer(buf, sizeof(buf), 0, kMilky);
  const uint32_t t0 = 4000000;
  TEST_ASSERT_TRUE(milky.hears(buf, n, t0));
  TEST_ASSERT_FALSE(milky.hears(buf, n, t0 + 1600));
  TEST_ASSERT_FALSE(milky.hears(buf, n, t0 + lamp_protocol::kDedupMaxAgeMs));
  TEST_ASSERT_TRUE(milky.hears(buf, n, t0 + lamp_protocol::kDedupMaxAgeMs + 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sender_seq_starts_at_drawn_value);
  RUN_TEST(test_sender_seq_draws_start_once);
  RUN_TEST(test_sender_seq_wraps_through_zero);
  RUN_TEST(test_field_lamps_drop_offers_after_seed_restarts_at_zero);
  RUN_TEST(test_field_lamp_drops_seq_it_only_overheard);
  RUN_TEST(test_field_lamps_accept_offers_when_each_boot_starts_random);
  RUN_TEST(test_current_lamp_takes_restarted_offer_after_dedup_age);
  return UNITY_END();
}
