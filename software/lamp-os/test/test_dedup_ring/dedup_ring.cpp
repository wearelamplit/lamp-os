// Native-host unit tests for DedupRing data behavior.
//
// Context: audit finding #7 (Stability finding #3) flagged the
// DedupRing instances inside ShowReceiver (helloDedup_, controlOpDedup_)
// as mutated from BOTH the WiFi recv task (Core 0) and the Arduino loop
// task (Core 1, via sendControlOp's record()). Without a mutex, two
// concurrent record() calls can corrupt the ring or cause memcmp to run
// against a half-written mac[].
//
// Fix in lamp_protocol.hpp: add a portMUX_TYPE mux_ and wrap the
// record()-body critical section (compare loop + slot write). The
// portMUX is firmware-only — the native host doesn't have FreeRTOS, so
// this test re-declares the ring inline (test_color / test_section_cache
// pattern). The point is to pin the DATA contract so the portMUX
// refactor can't accidentally regress the eviction order or the
// (mac, msgType, seq) keying.
//
// If you change the ring's keying or eviction policy in lamp_protocol.hpp,
// mirror here.

#include <unity.h>

#include <cstdint>
#include <cstring>

namespace lamp_protocol {

// Mirror of the production ring's data behavior. NO portMUX here —
// native host has no FreeRTOS. The production class wraps the
// record()-body in portENTER_CRITICAL/portEXIT_CRITICAL; the data flow
// (compare-then-evict-oldest) is what this test pins.
class DedupRing {
 public:
  // Mirror of the production CAPACITY. v0x03 mesh-deploy lock-in bumped
  // this from 32 → 64 because at 20-50 lamps each gossiping every unique
  // (sourceMac, seq), the 32-slot ring wrapped fast enough that a
  // late-arriving relayed copy could re-fire a receiver. The static_assert
  // pinning the production-side value lives in test_protocol_v2.
  // why: matches production lock-in per validated plan §"Layer 2".
  static constexpr size_t CAPACITY = 64;

  bool record(const uint8_t mac[6], uint8_t msgType, uint16_t seq) {
    for (size_t i = 0; i < CAPACITY; i++) {
      const Entry& e = entries_[i];
      if (e.used && e.msgType == msgType && e.seq == seq &&
          std::memcmp(e.mac, mac, 6) == 0) {
        return false;
      }
    }
    Entry& slot = entries_[head_];
    slot.used = true;
    slot.msgType = msgType;
    slot.seq = seq;
    std::memcpy(slot.mac, mac, 6);
    head_ = (head_ + 1) % CAPACITY;
    return true;
  }

 private:
  struct Entry {
    bool used = false;
    uint8_t msgType = 0;
    uint16_t seq = 0;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  };
  Entry entries_[CAPACITY];
  size_t head_ = 0;
};

}  // namespace lamp_protocol

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMacA[6] = {0xAA, 0x11, 0x22, 0x33, 0x44, 0x01};
static const uint8_t kMacB[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x02};

void test_first_record_returns_true_and_records() {
  // A fresh ring has seen nothing — the first record(mac, type, seq)
  // returns true so the receiver knows to handle (and rebroadcast) the
  // frame. A second identical record returns false.
  lamp_protocol::DedupRing ring;

  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 100));
}

void test_different_seq_is_independent() {
  // The dedup key is (mac, msgType, seq). Same mac + type but different
  // seq must be treated as a new frame.
  lamp_protocol::DedupRing ring;

  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 101));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 101));
}

void test_different_msgtype_is_independent() {
  // A HELLO and a COLORS frame with the same seq from the same mac
  // must NOT collide. (In practice seqs are per-type, but the keying
  // contract still has to honor msgType.)
  lamp_protocol::DedupRing ring;

  TEST_ASSERT_TRUE(ring.record(kMacA, 0x01, 50));
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 50));
}

void test_different_mac_is_independent() {
  // Two different sources broadcasting the same (msgType, seq) — both
  // are "new" to us.
  lamp_protocol::DedupRing ring;

  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 100));
  TEST_ASSERT_TRUE(ring.record(kMacB, 0x02, 100));
}

void test_full_ring_evicts_oldest_first() {
  // After CAPACITY records, the oldest slot is overwritten and that
  // entry's frame appears "new" again. Frames recorded MORE recently
  // than the oldest must still dedup.
  lamp_protocol::DedupRing ring;

  // Fill the ring with CAPACITY distinct (seq) entries from kMacA.
  for (uint16_t s = 0; s < lamp_protocol::DedupRing::CAPACITY; ++s) {
    TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, s));
  }

  // Right after filling: the FIRST entry (seq=0) is still in the ring
  // — re-recording it returns false. Same for seq=CAPACITY-1 (last).
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02,
                                lamp_protocol::DedupRing::CAPACITY - 1));

  // Insert one fresh entry — this evicts seq=0 (the oldest by
  // insertion order, occupying slot[head=0] after the fill loop wraps).
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02,
                               lamp_protocol::DedupRing::CAPACITY));

  // seq=0 is now evicted; recording it again returns true. This re-add
  // takes slot[1] (the next-oldest), so seq=1 is ALSO evicted as a
  // side effect.
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 0));

  // seq=2 is still in the ring — wasn't displaced yet.
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x02, 2));

  // seq=1 was evicted by the previous re-add, so it now looks new.
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x02, 1));
}

void test_full_ring_at_64_distinct_entries_all_record_then_evict_oldest() {
  // v0x03 lock-in boundary test: at the new CAPACITY=64, inserting 64
  // distinct entries must ALL return true (none evicted yet). A 65th
  // distinct insert evicts the oldest, allowing it to look new again.
  // This is the headroom argument for going 32→64: at 20-50 lamps with
  // per-msgType dedup, we want the ring big enough that a late-arriving
  // gossip copy (the one we relay through Commit E) still hits the dedup
  // before the slot rotates.
  // why: pins behavior at the new capacity boundary per validated plan §"Layer 2".
  lamp_protocol::DedupRing ring;

  // All 64 inserts must succeed.
  for (uint16_t s = 0; s < 64; ++s) {
    TEST_ASSERT_TRUE(ring.record(kMacA, 0x30, s));  // 0x30 = MSG_EVENT
  }
  // Oldest (seq=0) is still present at this point.
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x30, 0));

  // Insert the 65th distinct entry — evicts seq=0 (oldest by insertion order,
  // head=0 after the fill loop wraps once).
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x30, 64));

  // seq=0 now looks new again (was evicted). seq=63 still in-ring.
  TEST_ASSERT_TRUE(ring.record(kMacA, 0x30, 0));
  TEST_ASSERT_FALSE(ring.record(kMacA, 0x30, 63));
}

void test_empty_ring_does_not_match_zero_mac() {
  // Entry default-constructs with used=false. A record() lookup must
  // NOT match a default (zeroed) slot — otherwise a frame from mac
  // 00:00:00:00:00:00 with type=0 seq=0 would always look "seen".
  lamp_protocol::DedupRing ring;
  const uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};

  TEST_ASSERT_TRUE(ring.record(zeroMac, 0x00, 0));
  // Now it IS recorded — second call returns false. (We're proving the
  // unused-slot guard, not asserting on the first call's outcome alone.)
  TEST_ASSERT_FALSE(ring.record(zeroMac, 0x00, 0));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_first_record_returns_true_and_records);
  RUN_TEST(test_different_seq_is_independent);
  RUN_TEST(test_different_msgtype_is_independent);
  RUN_TEST(test_different_mac_is_independent);
  RUN_TEST(test_full_ring_evicts_oldest_first);
  RUN_TEST(test_full_ring_at_64_distinct_entries_all_record_then_evict_oldest);
  RUN_TEST(test_empty_ring_does_not_match_zero_mac);

  return UNITY_END();
}
