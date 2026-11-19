// NearbyList: the app's nearby list is held only while the app reads it, and
// when held its roster copy fits the lamps there and its rebuild reuses the
// served buffer.

#include <unity.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "components/network/mesh/lamp_roster.cpp"
#include "components/network/ble/nearby_list.cpp"
#include "components/network/ble/social_view.hpp"
#include "../replaced_new/replaced_new.hpp"

using ble_control::NearbyList;
using lamp::LampRoster;
using lamp::Color;
using lamp::ListedPeer;

static size_t g_allocs = 0;
static size_t g_liveBytes = 0;
static size_t g_peakBytes = 0;
static bool g_refuse = false;

constexpr size_t kHeader = alignof(std::max_align_t);

void* testAllocate(size_t n) {
  if (g_refuse) return nullptr;
  ++g_allocs;
  auto* block = static_cast<unsigned char*>(std::malloc(n + kHeader));
  if (!block) return nullptr;
  *reinterpret_cast<size_t*>(block) = n;
  g_liveBytes += n;
  if (g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
  return block + kHeader;
}
void testRelease(void* p) noexcept {
  auto* block = static_cast<unsigned char*>(p) - kHeader;
  g_liveBytes -= *reinterpret_cast<size_t*>(block);
  std::free(block);
}

namespace {

constexpr uint32_t kT0 = 100000;

LampRoster* g_roster = nullptr;
NearbyList* g_list = nullptr;
uint32_t g_now = kT0;
bool g_client = true;

void touch(LampRoster& r, uint8_t from, uint8_t to) {
  for (uint8_t i = from; i < to; i++) {
    char name[8];
    std::snprintf(name, sizeof(name), "lamp%02u", i);
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x03, i};
    r.addOrUpdateFromEspNow(name, mac, Color(i, 1, 2, 3), Color(4, 5, 6, i), 0x010203);
  }
}

// The loop's tick, run while a read waits.
void loopTick() { g_list->tick(*g_roster, g_client, g_now); }

void at(uint32_t ms) {
  g_now = ms;
  set_mock_millis(ms);
}

size_t entries(const std::string& json) {
  size_t n = 0;
  for (size_t pos = json.find("{\"name\":"); pos != std::string::npos;
       pos = json.find("{\"name\":", pos + 1)) {
    ++n;
  }
  return n;
}

struct Fixture {
  LampRoster roster;
  NearbyList list;
  Fixture() {
    g_roster = &roster;
    g_list = &list;
    at(kT0);
    touch(roster, 0, 22);
  }
};

}  // namespace

void setUp(void) {
  g_refuse = false;
  g_client = true;
  g_taskDelayHook = loopTick;
  g_mock_largest_free_block = 1 << 20;
}
void tearDown(void) {
  g_taskDelayHook = nullptr;
  g_refuse = false;
}

// A connected phone that never reads nearby costs no list and no roster copy.
void test_connected_without_a_read_holds_nothing() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  for (uint32_t t = kT0; t < kT0 + 60000; t += 250) {
    at(t);
    touch(f.roster, 0, 22);
    TEST_ASSERT_FALSE(f.list.tick(f.roster, true, t));
  }
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  std::string out;
  TEST_ASSERT_FALSE(f.list.copy(out));
  TEST_ASSERT_TRUE(out.empty());
}

// The read that asks for the list gets it, built by the loop while it waits.
void test_first_read_returns_the_list() {
  Fixture f;
  std::string out;
  TEST_ASSERT_TRUE(f.list.read(out, g_now));
  TEST_ASSERT_EQUAL_UINT(22, entries(out));
  TEST_ASSERT_EQUAL_CHAR('[', out.front());
  TEST_ASSERT_EQUAL_CHAR(']', out.back());
  TEST_ASSERT_EQUAL_UINT32(1, f.list.rev());
}

// Held while reads keep coming, freed kHoldMs after the last one.
void test_released_a_hold_after_the_last_read() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  std::string().swap(out);
  TEST_ASSERT_TRUE(g_liveBytes > baseline);
  at(kT0 + NearbyList::kHoldMs - 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_TRUE(f.list.built());
  f.list.read(out, g_now);
  std::string().swap(out);
  at(g_now + NearbyList::kHoldMs - 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_TRUE(f.list.built());
  at(g_now + 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
}

// Ending the hold frees the list and the roster copy on the next tick,
// well inside kHoldMs, and a later read builds it again.
void test_ended_hold_frees_on_the_next_tick() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  std::string().swap(out);
  TEST_ASSERT_TRUE(g_liveBytes > baseline);
  f.list.endHold();
  at(g_now + 50);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  TEST_ASSERT_FALSE(f.list.copy(out));
  at(g_now + 50);
  TEST_ASSERT_TRUE(f.list.read(out, g_now));
  TEST_ASSERT_EQUAL_UINT(22, entries(out));
}

// Through CHAR_EDIT_SESSION 0x08: a close after an open frees the list on the
// next tick, whether either came in OTA quiet; a close the app replays after
// the first, or after a disconnect, keeps a mesh-lamps screen's hold, in quiet
// too; an open waits out quiet, and other selectors leave the view alone.
void test_social_close_frees_and_a_replayed_close_keeps_the_hold() {
  ble_control::SocialView view;
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  auto readList = [&] {
    f.list.read(out, g_now);
    std::string().swap(out);
  };
  auto freedNextTick = [&] {
    at(g_now + 50);
    f.list.tick(f.roster, true, g_now);
    return !f.list.built() && g_liveBytes == baseline;
  };

  view.write(0x08, true, false, f.list);
  TEST_ASSERT_TRUE(view.active());
  readList();
  view.write(0x08, false, true, f.list);
  TEST_ASSERT_FALSE(view.active());
  TEST_ASSERT_TRUE(freedNextTick());

  readList();
  view.write(0x08, false, false, f.list);
  TEST_ASSERT_FALSE(freedNextTick());
  view.write(0x08, false, true, f.list);
  TEST_ASSERT_FALSE(freedNextTick());

  view.write(0x08, true, true, f.list);
  TEST_ASSERT_FALSE(view.active());
  view.write(0x08, false, true, f.list);
  TEST_ASSERT_TRUE(freedNextTick());

  view.write(0x08, true, true, f.list);
  view.write(0x08, true, false, f.list);
  TEST_ASSERT_TRUE(view.active());
  view.write(0x01, false, false, f.list);
  TEST_ASSERT_TRUE(view.active());

  readList();
  view.clear();
  TEST_ASSERT_FALSE(view.active());
  view.write(0x08, false, true, f.list);
  TEST_ASSERT_FALSE(freedNextTick());
}

// A disconnect frees it and ends the hold, so a reconnect holds nothing
// until a read; every fresh build moves the rev so the app re-reads.
void test_disconnect_frees_and_reconnect_waits_for_a_read() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  std::string().swap(out);
  at(g_now + 100);
  f.list.tick(f.roster, false, g_now);
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  at(g_now + 5000);
  TEST_ASSERT_FALSE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  TEST_ASSERT_TRUE(f.list.read(out, g_now));
  TEST_ASSERT_EQUAL_UINT32(2, f.list.rev());
}

// A list that shrinks while the heap is too tight for a smaller buffer still
// refreshes, into the larger one held.
void test_low_heap_shrink_refreshes_in_place() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  at(g_now + 300000);
  touch(f.roster, 0, 4);
  f.roster.prune(LAMP_PRUNE_TIME_MS);
  g_mock_largest_free_block = 0;
  f.list.read(out, g_now);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL_UINT(4, entries(out));
}

void test_read_at_millis_zero_counts() {
  Fixture f;
  g_taskDelayHook = nullptr;
  std::string out;
  f.list.read(out, 0);
  f.list.tick(f.roster, true, 1);
  TEST_ASSERT_TRUE(f.list.built());
}

// A stamp left behind expires even with no phone connected, so a millis
// wrap ~49.7 days later cannot revive it.
void test_expired_read_never_revives_after_wrap() {
  Fixture f;
  g_taskDelayHook = nullptr;
  std::string out;
  f.list.read(out, kT0);
  f.list.tick(f.roster, false, kT0 + NearbyList::kHoldMs);
  f.list.tick(f.roster, true, kT0 + 1);  // kT0 + 2^32 + 1
  TEST_ASSERT_FALSE(f.list.built());
}

// While built, a roster change rebuilds within a second and moves the rev;
// an rssi or lastSeen refresh rebuilds without moving it.
void test_change_while_open_moves_the_rev() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  const uint32_t rev = f.list.rev();
  touch(f.roster, 22, 23);
  at(g_now + 500);
  TEST_ASSERT_FALSE(f.list.tick(f.roster, true, g_now));
  at(g_now + 500);
  TEST_ASSERT_TRUE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, f.list.rev());
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL_UINT(23, entries(out));

  at(g_now + 1000);
  touch(f.roster, 0, 23);
  TEST_ASSERT_FALSE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, f.list.rev());
}

// Steady rebuilds render into the buffer already held.
void test_steady_rebuilds_allocate_nothing() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  for (int i = 0; i < 20; i++) {
    at(g_now + 1000);
    touch(f.roster, 0, 22);
    g_allocs = 0;
    f.list.tick(f.roster, true, g_now);
    TEST_ASSERT_EQUAL_UINT(0, g_allocs);
  }
  f.list.read(out, g_now);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        out.find("\"lastSeenMs\":" + std::to_string(g_now)));
}

// The held list follows the lamps there, growing and shrinking.
void test_held_list_follows_the_roster() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  std::string().swap(out);
  const size_t at22 = g_liveBytes - baseline;
  touch(f.roster, 22, 25);
  at(g_now + 1000);
  f.list.tick(f.roster, true, g_now);
  const size_t at25 = g_liveBytes - baseline;
  TEST_ASSERT_TRUE(at25 > at22);
  at(g_now + 300000);
  touch(f.roster, 0, 4);
  f.roster.prune(LAMP_PRUNE_TIME_MS);
  f.list.read(out, g_now);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL_UINT(4, entries(out));
  std::string().swap(out);
  const size_t at4 = g_liveBytes - baseline;
  TEST_ASSERT_TRUE(at4 < at22 / 4);
}

// Resizing the roster copy frees the old block before taking the new one, so
// the two are never live together.
void test_roster_copy_resize_never_holds_both() {
  Fixture f;
  f.roster.getAll();
  at(g_now + 300000);
  touch(f.roster, 0, 10);
  f.roster.prune(LAMP_PRUNE_TIME_MS);
  const size_t before = g_liveBytes;
  g_peakBytes = before;
  TEST_ASSERT_EQUAL_UINT(10, f.roster.getAll().capacity());
  TEST_ASSERT_EQUAL_UINT(before, g_peakBytes);
}

// A heap too tight for a bigger buffer still refreshes into the one held.
void test_low_heap_still_refreshes_in_place() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  g_mock_largest_free_block = 0;
  at(g_now + 1000);
  touch(f.roster, 0, 22);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        out.find("\"lastSeenMs\":" + std::to_string(g_now)));
}

// No heap for the first build: the read ends empty after its wait.
void test_low_heap_first_read_is_empty() {
  Fixture f;
  g_mock_largest_free_block = 0;
  std::string out = "stale";
  TEST_ASSERT_FALSE(f.list.read(out, g_now));
  TEST_ASSERT_TRUE(out.empty());
}

// The measure the build sizes from matches the render byte for byte, every
// field at its longest.
void test_measured_length_matches_the_render() {
  ListedPeer p;
  std::memset(p.name, 0x01, sizeof(p.name) - 1);
  std::memset(p.fwChannel, 0x02, sizeof(p.fwChannel) - 1);
  std::memset(p.mac, 0xAB, 6);
  std::memset(p.otaSendingTo, 0xCD, 6);
  p.hasMac = true;
  p.hasOtaSendingTo = true;
  p.lastSeenNearMs = 4294967295u;
  p.lastSeenMeshMs = 4294967295u;
  p.firmwareVersion = 4294967295u;
  p.lastRssi = -128;
  p.espnowRssi = -128;
  p.otaState = 255;
  std::vector<ListedPeer> lamps(NearbyList::kMaxLamps, p);
  std::string out;
  NearbyList::render(lamps, 4294967295u, out);
  TEST_ASSERT_EQUAL_UINT(out.size(), ble_control::renderedLength(lamps, 4294967295u));
}

// The first build asks the heap for the list's own length plus a small
// slack, so a block just that big serves it.
void test_build_demand_tracks_the_list_length() {
  size_t length = 0;
  {
    Fixture f;
    std::string out;
    TEST_ASSERT_TRUE(f.list.read(out, g_now));
    length = out.size();
  }
  Fixture f;
  TEST_ASSERT_TRUE(ble_control::kBuildSlack <= length / 16);
  g_mock_largest_free_block = length + ble_control::kBuildSlack + ble_control::kHeapMargin;
  std::string out;
  TEST_ASSERT_TRUE(f.list.read(out, g_now));
  TEST_ASSERT_EQUAL_UINT(length, out.size());
}

// A read stamped on another core just after the loop sampled its clock is
// fresh, not 49 days old.
void test_read_stamped_after_the_tick_clock_counts() {
  Fixture f;
  g_taskDelayHook = nullptr;
  std::string out;
  f.list.read(out, g_now + 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_TRUE(f.list.built());
}

// Each app-visible field moves the rev when it changes.
void test_each_visible_field_moves_the_rev() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x03, 0};
  const uint8_t target[6] = {0x02, 0x00, 0x00, 0x00, 0x03, 9};
  struct Change {
    const char* what;
    void (*apply)(LampRoster&, const uint8_t*, const uint8_t*);
  };
  static const Change kChanges[] = {
      {"base", [](LampRoster& r, const uint8_t* m, const uint8_t*) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(4, 5, 6, 0), 0x010203);
       }},
      {"shade", [](LampRoster& r, const uint8_t* m, const uint8_t*) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010203);
       }},
      {"fwVersion", [](LampRoster& r, const uint8_t* m, const uint8_t*) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204);
       }},
      {"otaState", [](LampRoster& r, const uint8_t* m, const uint8_t*) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204, 2);
       }},
      {"fwChannel", [](LampRoster& r, const uint8_t* m, const uint8_t*) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204, 2,
                                 5, "standard-beta");
       }},
      {"otaSendingTo", [](LampRoster& r, const uint8_t* m, const uint8_t* t) {
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204, 2,
                                 5, "standard-beta", nullptr, false, 0, false, t, true);
       }},
      {"otaSendingTo target", [](LampRoster& r, const uint8_t* m, const uint8_t* t) {
         const uint8_t other[6] = {t[0], t[1], t[2], t[3], t[4], 10};
         r.addOrUpdateFromEspNow("lamp00", m, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204, 2,
                                 5, "standard-beta", nullptr, false, 0, false, other, true);
       }},
      {"near", [](LampRoster& r, const uint8_t* m, const uint8_t*) { r.markNear(m); }},
  };
  for (const Change& c : kChanges) {
    const uint32_t rev = f.list.rev();
    c.apply(f.roster, mac, target);
    at(g_now + 1000);
    f.list.tick(f.roster, true, g_now);
    f.list.read(out, g_now);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(rev + 1, f.list.rev(), c.what);
  }
  // Near lapses while the peer stays listed over the mesh.
  const uint32_t rev = f.list.rev();
  at(g_now + LAMP_PRUNE_TIME_MS + 1000);
  const uint8_t other[6] = {target[0], target[1], target[2], target[3], target[4], 10};
  f.roster.addOrUpdateFromEspNow("lamp00", mac, Color(9, 1, 2, 3), Color(9, 5, 6, 0), 0x010204,
                                 2, 5, "standard-beta", nullptr, false, 0, false, other, true);
  f.list.read(out, g_now);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(rev + 1, f.list.rev(), "near lapsed");
}

// fwChannel is ASCII by contract, so a byte past 0x7F renders escaped and the
// list stays valid UTF-8.
void test_fw_channel_high_bytes_are_escaped() {
  ListedPeer p;
  std::strcpy(p.name, "x");
  p.fwChannel[0] = static_cast<char>(0xFF);
  p.fwChannel[1] = '\0';
  std::string out;
  NearbyList::render({p}, 0, out);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("\"fwChannel\":\"\\u00FF\""));
  for (const char c : out) TEST_ASSERT_TRUE(static_cast<unsigned char>(c) < 0x80);
}

// A peer's direct ESP-NOW reading renders as `mrssi` beside the BLE `rssi`,
// and is left out when there is none.
void test_direct_reading_renders_as_mrssi() {
  ListedPeer p;
  std::strcpy(p.name, "x");
  p.lastRssi = -61;
  p.espnowRssi = -88;
  std::string out;
  NearbyList::render({p}, 0, out);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find(",\"mrssi\":-88"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find(",\"rssi\":-61"));
  p.espnowRssi = -127;
  NearbyList::render({p}, 0, out = "");
  TEST_ASSERT_EQUAL(std::string::npos, out.find("mrssi"));
}

// A held list no roster change touches still drops a peer's mrssi within a
// second of the reading going stale, and keeps it until then.
void test_stale_mrssi_leaves_a_list_no_change_rebuilds() {
  Fixture f;
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x03, 0};
  const uint32_t heard = g_now + 3700;
  at(heard);
  f.roster.addOrUpdateFromEspNow("lamp00", mac, Color(0, 1, 2, 3), Color(4, 5, 6, 0), 0x010203,
                                 0, 5, nullptr, nullptr, false, 0, false, nullptr, false, -71);
  at(heard + 250);
  std::string out;
  f.list.read(out, g_now);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("\"mrssi\":-71"));
  const uint32_t staleAt = heard + lamp::DirectRssi::kMaxAgeMs + 1;
  for (uint32_t t = heard + 1250; t < staleAt; t += 1000) {
    at(t);
    f.list.tick(f.roster, true, g_now);
    f.list.read(out, g_now);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(std::string::npos, out.find("\"mrssi\":-71"), "fresh");
  }
  at(staleAt + 1000);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL(std::string::npos, out.find("mrssi"));
}

// fwChannel arrives over the air, so a quote in it cannot end the string.
void test_fw_channel_is_escaped() {
  ListedPeer p;
  std::strcpy(p.name, "x");
  std::strcpy(p.fwChannel, "a\",\"z");
  std::string out;
  NearbyList::render({p}, 0, out);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("\"fwChannel\":\"a\\\",\\\"z\""));
}

// A copy the heap refuses gives the lock back, so the loop is never stuck.
void test_refused_copy_gives_the_lock_back() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  std::string().swap(out);
  g_refuse = true;
  bool threw = false;
  try {
    f.list.copy(out);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  g_refuse = false;
  TEST_ASSERT_TRUE(threw);
  TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connected_without_a_read_holds_nothing);
  RUN_TEST(test_first_read_returns_the_list);
  RUN_TEST(test_released_a_hold_after_the_last_read);
  RUN_TEST(test_disconnect_frees_and_reconnect_waits_for_a_read);
  RUN_TEST(test_low_heap_shrink_refreshes_in_place);
  RUN_TEST(test_ended_hold_frees_on_the_next_tick);
  RUN_TEST(test_social_close_frees_and_a_replayed_close_keeps_the_hold);
  RUN_TEST(test_read_at_millis_zero_counts);
  RUN_TEST(test_expired_read_never_revives_after_wrap);
  RUN_TEST(test_change_while_open_moves_the_rev);
  RUN_TEST(test_steady_rebuilds_allocate_nothing);
  RUN_TEST(test_held_list_follows_the_roster);
  RUN_TEST(test_roster_copy_resize_never_holds_both);
  RUN_TEST(test_low_heap_still_refreshes_in_place);
  RUN_TEST(test_low_heap_first_read_is_empty);
  RUN_TEST(test_measured_length_matches_the_render);
  RUN_TEST(test_build_demand_tracks_the_list_length);
  RUN_TEST(test_fw_channel_is_escaped);
  RUN_TEST(test_direct_reading_renders_as_mrssi);
  RUN_TEST(test_stale_mrssi_leaves_a_list_no_change_rebuilds);
  RUN_TEST(test_read_stamped_after_the_tick_clock_counts);
  RUN_TEST(test_each_visible_field_moves_the_rev);
  RUN_TEST(test_fw_channel_high_bytes_are_escaped);
  RUN_TEST(test_refused_copy_gives_the_lock_back);
  return UNITY_END();
}
