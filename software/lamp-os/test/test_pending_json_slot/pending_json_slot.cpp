// Native-host unit tests for the PendingJsonSlot<N> template.
//
// Context: audit finding #9. Nine duplicated "post → memcpy under portMUX →
// drain → parse → dispatch" idioms in lamp.cpp. The template
// consolidates the bounded-buffer + valid-bit + length triple into one
// shape with two operations (post, drain). All NVS / JSON parse / dispatch
// work remains in the per-slot drain block — the template only owns
// the byte-copy hop.
//
// Following test_section_cache convention: the slot is re-declared inline
// here as a tiny test-only struct that mirrors the production template's
// data-shape and contracts. The native runner doesn't have portMUX_TYPE
// nor portENTER_CRITICAL, so the test substitutes a no-op mux. The
// production header guards the same logic with the actual portMUX calls;
// the contract being exercised here is bounds-check + length + NUL-term +
// valid-bit ordering, not the OS primitive itself.
//
// If you change the shape of PendingJsonSlot in src/lamps/pending_json_slot.hpp
// (length type, NUL semantics, oversize behavior), mirror it here.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Native test substitute for portMUX_TYPE + the critical-section macros.
// The production template takes portMUX_TYPE& by reference; here we pass an
// empty struct and the macros are no-ops. The behavior under test is the
// data-shape contract, not the OS-level mutex.
// ---------------------------------------------------------------------------

struct TestMux {};
#define portENTER_CRITICAL(x) do { (void)(x); } while (0)
#define portEXIT_CRITICAL(x)  do { (void)(x); } while (0)
using portMUX_TYPE = TestMux;

#include "core/pending_json_slot.hpp"

void setUp(void) {}
void tearDown(void) {}

void test_fresh_slot_is_not_valid() {
  lamp::PendingJsonSlot<64> slot;
  TEST_ASSERT_FALSE(slot.valid);
  TEST_ASSERT_EQUAL_UINT16(0, slot.length);
}

void test_post_then_drain_round_trip() {
  // The minimum viable contract: post the bytes, drain into a buffer that's
  // N+1 long, get the length back, get a NUL-terminated string.
  lamp::PendingJsonSlot<64> slot;
  portMUX_TYPE mux{};

  const char* payload = "{\"hi\":1}";
  TEST_ASSERT_TRUE(slot.post(mux, payload, strlen(payload)));
  TEST_ASSERT_TRUE(slot.valid);

  char buf[64 + 1];
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(strlen(payload), len);
  TEST_ASSERT_EQUAL_STRING(payload, buf);
  TEST_ASSERT_FALSE(slot.valid);
}

void test_drain_on_empty_slot_returns_zero() {
  // Hot path: every loop iteration polls every slot. drain() on a !valid
  // slot must return 0 without touching the buffer or mutating state.
  lamp::PendingJsonSlot<64> slot;
  portMUX_TYPE mux{};

  char buf[64 + 1] = {'X', 'X', 'X'};
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(0, len);
  // Buffer untouched (the production drain block's NUL write only runs
  // after a successful copy; mirror that contract here).
  TEST_ASSERT_EQUAL_CHAR('X', buf[0]);
}

void test_post_oversize_is_rejected() {
  // Defense-in-depth: the BLE callback already bounds-checks against the
  // characteristic's max-size, but the slot must reject anything > N
  // without touching its own buffer or flipping valid.
  lamp::PendingJsonSlot<8> slot;
  portMUX_TYPE mux{};

  char giant[16];
  memset(giant, 'A', sizeof(giant));
  TEST_ASSERT_FALSE(slot.post(mux, giant, sizeof(giant)));
  TEST_ASSERT_FALSE(slot.valid);
  TEST_ASSERT_EQUAL_UINT16(0, slot.length);
}

void test_post_exactly_n_is_accepted() {
  // Boundary: len == N is the maximum legal post. The slot has N bytes of
  // storage; the caller's drain buffer is N+1 so the trailing NUL fits.
  lamp::PendingJsonSlot<8> slot;
  portMUX_TYPE mux{};

  const char* payload = "abcdefgh";  // exactly 8 chars
  TEST_ASSERT_TRUE(slot.post(mux, payload, 8));
  TEST_ASSERT_TRUE(slot.valid);

  char buf[8 + 1];
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(8, len);
  TEST_ASSERT_EQUAL_CHAR('\0', buf[8]);
  TEST_ASSERT_EQUAL_STRING_LEN(payload, buf, 8);
}

void test_drain_nul_terminates_at_length() {
  // The drain block in lamp.cpp does `buf[len] = '\0'` after the
  // memcpy. The template MUST own that NUL write so the duplicated
  // bodies don't all need to remember it. Pre-fill the buf with garbage
  // and verify the byte at index `len` is NUL even if no NUL was posted.
  lamp::PendingJsonSlot<32> slot;
  portMUX_TYPE mux{};

  const char payload[] = "no-trailing-nul";  // 15 chars, no terminator promise
  slot.post(mux, payload, 15);

  char buf[32 + 1];
  memset(buf, '?', sizeof(buf));
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(15, len);
  TEST_ASSERT_EQUAL_CHAR('\0', buf[15]);
}

void test_post_overwrites_previous_unread_post() {
  // The BLE callback fires faster than the loop drains under continuous
  // slider drag. Newest write wins — the slot is single-slot, not a queue,
  // and the contract is "last writer's bytes survive".
  lamp::PendingJsonSlot<32> slot;
  portMUX_TYPE mux{};

  TEST_ASSERT_TRUE(slot.post(mux, "first", 5));
  TEST_ASSERT_TRUE(slot.post(mux, "second", 6));
  TEST_ASSERT_TRUE(slot.valid);

  char buf[32 + 1];
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(6, len);
  TEST_ASSERT_EQUAL_STRING("second", buf);
}

void test_drain_then_post_then_drain() {
  // After a successful drain the slot is empty; a fresh post must work
  // identically to the first-ever post. Verifies valid-bit + length are
  // reset cleanly on drain.
  lamp::PendingJsonSlot<32> slot;
  portMUX_TYPE mux{};

  slot.post(mux, "round-1", 7);
  char buf[32 + 1];
  TEST_ASSERT_EQUAL_UINT16(7, slot.drain(mux, buf));

  slot.post(mux, "round-2-longer", 14);
  TEST_ASSERT_TRUE(slot.valid);
  TEST_ASSERT_EQUAL_UINT16(14, slot.drain(mux, buf));
  TEST_ASSERT_EQUAL_STRING("round-2-longer", buf);
}

void test_post_zero_length_is_accepted() {
  // CHAR_EXPRESSION_TEST allows an empty payload as a "test complete"
  // sentinel — the slot must accept len==0 (it's the existing wire
  // contract; the testAction drain treats len==0 specially).
  lamp::PendingJsonSlot<64> slot;
  portMUX_TYPE mux{};

  TEST_ASSERT_TRUE(slot.post(mux, "", 0));
  TEST_ASSERT_TRUE(slot.valid);

  char buf[64 + 1];
  uint16_t len = slot.drain(mux, buf);
  TEST_ASSERT_EQUAL_UINT16(0, len);
  TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
  TEST_ASSERT_FALSE(slot.valid);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_fresh_slot_is_not_valid);
  RUN_TEST(test_post_then_drain_round_trip);
  RUN_TEST(test_drain_on_empty_slot_returns_zero);
  RUN_TEST(test_post_oversize_is_rejected);
  RUN_TEST(test_post_exactly_n_is_accepted);
  RUN_TEST(test_drain_nul_terminates_at_length);
  RUN_TEST(test_post_overwrites_previous_unread_post);
  RUN_TEST(test_drain_then_post_then_drain);
  RUN_TEST(test_post_zero_length_is_accepted);

  return UNITY_END();
}
