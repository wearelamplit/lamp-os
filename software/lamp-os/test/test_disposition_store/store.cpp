// Native-host tests for DispositionStore: get/set/clamp/eviction, the JSON
// bulk replace, and a persist/reload round trip through an InMemoryConfigStore
// (real persistence logic, no flash).

#include <unity.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

#include "config/config_store.hpp"
#include "config/disposition_store.hpp"
#include "util/bd_addr.hpp"

// Native tests don't build src/, so compile the real implementation in.
#include "../../src/config/disposition_store.cpp"
#include "../../src/util/heap_guard.cpp"

// Allocations fail while set, the way a fragmented heap refuses one.
static bool g_failAllocations = false;
static int g_allocations = 0;
void* operator new(size_t size) {
  ++g_allocations;
  if (g_failAllocations) throw std::bad_alloc();
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using namespace lamp;

// The store keys on the raw 6-byte MAC; unpack a canonical colon-hex literal
// so the tests can keep reading like the on-wire form.
static std::array<uint8_t, 6> mac(const char* s) {
  std::array<uint8_t, 6> m{};
  TEST_ASSERT_TRUE(parseBdAddr(s, m.data()));
  return m;
}
static uint8_t getMac(const DispositionStore& ds, const char* s) {
  return ds.peer(mac(s).data(), 0).disp;
}
static void setMac(DispositionStore& ds, const char* s, uint8_t v, uint64_t t) {
  ds.set(mac(s).data(), v, t);
}

void setUp(void) {}
void tearDown(void) {}

void test_unknown_peer_returns_default() {
  DispositionStore ds(100);
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kDefault, getMac(ds, "AA:BB:CC:DD:EE:FF"));
}

void test_keyed_by_mac() {
  DispositionStore ds(100);
  setMac(ds, "C4:DD:57:EB:64:60", 4, 0);
  TEST_ASSERT_EQUAL_UINT8(4, getMac(ds, "C4:DD:57:EB:64:60"));
}

void test_set_get_and_clamp() {
  DispositionStore ds(100);
  setMac(ds, "AA:BB:CC:DD:EE:01", 4, 0);
  TEST_ASSERT_EQUAL_UINT8(4, getMac(ds, "AA:BB:CC:DD:EE:01"));
  setMac(ds, "AA:BB:CC:DD:EE:02", 9, 0);  // clamps to 5
  TEST_ASSERT_EQUAL_UINT8(5, getMac(ds, "AA:BB:CC:DD:EE:02"));
  setMac(ds, "AA:BB:CC:DD:EE:03", 0, 0);  // clamps to 1
  TEST_ASSERT_EQUAL_UINT8(1, getMac(ds, "AA:BB:CC:DD:EE:03"));
}

void test_set_from_json_filters_invalid_keys() {
  DispositionStore ds(100);
  const char* json =
      "{\"AA:BB:CC:DD:EE:01\":4,\"not-a-bdaddr\":2,\"AA:BB:CC:DD:EE:02\":5}";
  TEST_ASSERT_TRUE(ds.setFromJson(json, std::string(json).size(), 0));
  TEST_ASSERT_EQUAL_UINT8(4, getMac(ds, "AA:BB:CC:DD:EE:01"));
  TEST_ASSERT_EQUAL_UINT8(5, getMac(ds, "AA:BB:CC:DD:EE:02"));
}

void test_debounced_persist_and_reload_round_trip() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);  // 100 ms idle window
  ds.attachStore(&backing);

  setMac(ds, "AA:BB:CC:DD:EE:01", 4, 1000);
  ds.maybeFlush(1050);  // within window → no write yet
  TEST_ASSERT_FALSE(backing.has(DispositionStore::kBlobKey));
  ds.maybeFlush(1100);  // window elapsed → writes
  TEST_ASSERT_TRUE(backing.has(DispositionStore::kBlobKey));

  // A fresh store over the same backing loads the persisted value.
  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load(0);
  TEST_ASSERT_EQUAL_UINT8(4, getMac(reloaded, "AA:BB:CC:DD:EE:01"));
}

void test_flush_now_writes_immediately() {
  InMemoryConfigStore backing;
  DispositionStore ds(100000);  // long window; only flushNow should write
  ds.attachStore(&backing);
  setMac(ds, "AA:BB:CC:DD:EE:09", 4, 0);
  ds.flushNow(0);
  TEST_ASSERT_TRUE(backing.has(DispositionStore::kBlobKey));
}

// The on-disk JSON must stay byte-identical across the byte-key packing:
// canonical uppercase colon-hex keys, ascending, compact. Byte order of the
// packed MAC matches the old string order, so the ':' separator (0x3A, between
// '9' and 'A') and per-byte hex ordering are preserved.
void test_asjson_is_sorted_canonical_colon_hex() {
  DispositionStore ds(100);
  setMac(ds, "FF:EE:DD:CC:BB:AA", 5, 0);
  setMac(ds, "AA:BB:CC:DD:EE:09", 2, 0);
  setMac(ds, "AA:BB:CC:DD:EE:0A", 4, 0);  // 0x0A sorts after 0x09
  TEST_ASSERT_EQUAL_STRING(
      "{\"AA:BB:CC:DD:EE:09\":2,\"AA:BB:CC:DD:EE:0A\":4,\"FF:EE:DD:CC:BB:AA\":5}",
      ds.asJson().c_str());
}

// An existing persisted hex-string blob (as older firmware wrote it) loads
// into the byte-keyed store with every disposition preserved, and re-serializes
// to the same canonical bytes — proving the upgrade keeps learned dispositions.
void test_loads_existing_persisted_blob() {
  InMemoryConfigStore backing;
  backing.write("dispositions",
                "{\"11:22:33:44:55:66\":1,\"C4:DD:57:EB:64:60\":4}");
  DispositionStore ds(100);
  ds.attachStore(&backing);
  ds.load(0);
  TEST_ASSERT_EQUAL_UINT8(1, getMac(ds, "11:22:33:44:55:66"));
  TEST_ASSERT_EQUAL_UINT8(4, getMac(ds, "C4:DD:57:EB:64:60"));
  TEST_ASSERT_EQUAL_STRING(
      "{\"11:22:33:44:55:66\":1,\"C4:DD:57:EB:64:60\":4}", ds.asJson().c_str());
}

// A full NVS keeps the entries dirty and backs off 5 s doubling to 60 s, so a
// failing flush can never retry at loop rate.
void test_failed_write_backs_off_then_lands() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  setMac(ds, "AA:BB:CC:DD:EE:01", 5, 0);
  backing.failWrites = true;
  for (uint32_t t = 100; t < 4900; t += 10) ds.maybeFlush(t);
  TEST_ASSERT_EQUAL_UINT(1, backing.writeCount);
  ds.maybeFlush(5100);  // first retry, 5 s after the failure
  TEST_ASSERT_EQUAL_UINT(2, backing.writeCount);
  for (uint32_t t = 5110; t < 15100; t += 10) ds.maybeFlush(t);
  TEST_ASSERT_EQUAL_UINT(2, backing.writeCount);  // second wait is 10 s
  ds.flushNow(15000);  // a disconnect flush is held to the backoff too
  TEST_ASSERT_EQUAL_UINT(2, backing.writeCount);
  backing.failWrites = false;
  ds.maybeFlush(15100);
  TEST_ASSERT_EQUAL_UINT(3, backing.writeCount);
  TEST_ASSERT_TRUE(backing.has(DispositionStore::kBlobKey));
  ds.maybeFlush(200000);  // clean after the success
  TEST_ASSERT_EQUAL_UINT(3, backing.writeCount);
}

// A lamp updating from the JSON form keeps its dispositions; the first flush
// writes the blob and erases the JSON key.
void test_legacy_json_migrates_on_first_flush() {
  InMemoryConfigStore backing;
  backing.write("dispositions", "{\"11:22:33:44:55:66\":1,\"C4:DD:57:EB:64:60\":4}");
  DispositionStore ds(100);
  ds.attachStore(&backing);
  ds.load(0);
  setMac(ds, "AA:BB:CC:DD:EE:01", 2, 0);
  ds.maybeFlush(100);
  TEST_ASSERT_FALSE(backing.has("dispositions"));
  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load(0);
  TEST_ASSERT_EQUAL_UINT8(1, getMac(reloaded, "11:22:33:44:55:66"));
  TEST_ASSERT_EQUAL_UINT8(4, getMac(reloaded, "C4:DD:57:EB:64:60"));
  TEST_ASSERT_EQUAL_UINT8(2, getMac(reloaded, "AA:BB:CC:DD:EE:01"));
}

// The legacy JSON migrates at load, blob first: a reset between the two
// writes still leaves a readable copy, and the JSON's space comes back on the
// first boot.
void test_legacy_json_migrates_at_load_blob_first() {
  for (size_t crashAt = 0; crashAt < 3; ++crashAt) {
    InMemoryConfigStore backing;
    backing.write("dispositions", "{\"11:22:33:44:55:66\":1}");
    backing.writesBeforeCrash = crashAt;
    DispositionStore ds(100);
    ds.attachStore(&backing);
    ds.load(0);
    backing.writesBeforeCrash = SIZE_MAX;
    DispositionStore reloaded(100);
    reloaded.attachStore(&backing);
    reloaded.load(0);
    TEST_ASSERT_EQUAL_UINT8(1, getMac(reloaded, "11:22:33:44:55:66"));
  }
  InMemoryConfigStore backing;
  backing.write("dispositions", "{\"11:22:33:44:55:66\":1}");
  DispositionStore ds(100);
  ds.attachStore(&backing);
  ds.load(0);
  TEST_ASSERT_FALSE(backing.has("dispositions"));
  TEST_ASSERT_TRUE(backing.has(DispositionStore::kBlobKey));
}

// After a downgrade an older firmware writes the JSON key again; flashed
// forward, that newer copy wins over the stale blob.
void test_json_written_after_a_downgrade_wins() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  setMac(ds, "11:22:33:44:55:66", 1, 0);
  ds.flushNow(0);
  backing.write("dispositions", "{\"11:22:33:44:55:66\":5}");
  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load(0);
  TEST_ASSERT_EQUAL_UINT8(5, getMac(reloaded, "11:22:33:44:55:66"));
}

// A corrupt or empty legacy JSON never replaces a blob that holds entries.
void test_corrupt_or_empty_json_keeps_the_blob() {
  for (const char* bad : {"{\"11:22", "{}"}) {
    InMemoryConfigStore backing;
    DispositionStore ds(100);
    ds.attachStore(&backing);
    setMac(ds, "11:22:33:44:55:66", 4, 0);
    ds.flushNow(0);
    backing.write("dispositions", bad);
    DispositionStore reloaded(100);
    reloaded.attachStore(&backing);
    reloaded.load(0);
    TEST_ASSERT_EQUAL_UINT8(4, getMac(reloaded, "11:22:33:44:55:66"));
    DispositionStore again(100);
    again.attachStore(&backing);
    again.load(0);
    TEST_ASSERT_EQUAL_UINT8(4, getMac(again, "11:22:33:44:55:66"));
    TEST_ASSERT_FALSE(backing.has("dispositions"));
  }
}

void test_asjson_sized_to_the_entries() {
  DispositionStore ds(100);
  setMac(ds, "AA:BB:CC:DD:EE:01", 2, 0);
  const std::string json = ds.asJson();
  TEST_ASSERT_EQUAL_STRING("{\"AA:BB:CC:DD:EE:01\":2}", json.c_str());
}

static uint8_t nonNeutral(unsigned i) {
  static constexpr uint8_t kValues[4] = {1, 2, 4, 5};
  return kValues[i % 4];
}

// A full store of user rows round-trips through the blob at its ceiling.
void test_full_store_blob_round_trip() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    const uint8_t m[6] = {1, 2, 3, 4, 0, static_cast<uint8_t>(i)};
    ds.set(m, nonNeutral(i), 0);
  }
  ds.flushNow(0);
  TEST_ASSERT_EQUAL(DispositionStore::kMaxBlobLen,
                    backing.read(DispositionStore::kBlobKey, "").size());
  DispositionStore reloaded(100);
  reloaded.attachStore(&backing);
  reloaded.load(0);
  TEST_ASSERT_EQUAL_STRING(ds.asJson().c_str(), reloaded.asJson().c_str());
}

// The direct writer emits exactly what ArduinoJson serialized for the same
// object, at full capacity, so a stored blob reads back unchanged.
void test_full_store_matches_arduinojson_bytes() {
  DispositionStore ds(100);
  JsonDocument doc;
  char key[18];
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    const uint8_t m[6] = {0x10, 0x20, 0x30, 0x40, static_cast<uint8_t>(i >> 8),
                          static_cast<uint8_t>(i)};
    const uint8_t v = nonNeutral(i);
    ds.set(m, v, 0);
    formatBdAddr(m, key);
    doc[key] = v;
  }
  std::string expected;
  serializeJson(doc, expected);
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), ds.asJson().c_str());
}

void test_empty_store_serializes_empty_object() {
  DispositionStore ds(100);
  TEST_ASSERT_EQUAL_STRING("{}", ds.asJson().c_str());
}

// A failed allocation for the JSON leaves no lock held: the next caller on
// either core can still take it.
void test_a_failed_json_allocation_gives_the_lock_back() {
  InMemoryConfigStore store;
  DispositionStore ds(100);
  ds.attachStore(&store);
  const char* entries = "{\"11:22:33:44:55:66\":1,\"C4:DD:57:EB:64:60\":4}";
  TEST_ASSERT_TRUE(ds.setFromJson(entries, std::strlen(entries), 0));
  const int heldBefore = g_semaphoresHeld;
  std::string json = "x";
  g_failAllocations = true;
  try {
    json = ds.asJson();
  } catch (const std::bad_alloc&) {
  }
  g_failAllocations = false;
  TEST_ASSERT_EQUAL(heldBefore, g_semaphoresHeld);
  TEST_ASSERT_TRUE(json.empty());
  TEST_ASSERT_EQUAL_STRING(entries, ds.asJson().c_str());
}

// An entry added on the other core between sizing the JSON and writing it
// (the second take) makes the write wait for a correctly sized block.
namespace {
DispositionStore* g_growing = nullptr;
int g_takes = 0;
void growOnSecondTake() {
  if (++g_takes != 2) return;
  g_semaphoreTakeHook = nullptr;
  const uint8_t mac[6] = {0xC4, 0xDD, 0x57, 0xEB, 0x64, 0x60};
  g_growing->set(mac, 4, 0);
}
}  // namespace

void test_an_entry_added_between_the_locks_is_included() {
  InMemoryConfigStore store;
  DispositionStore ds(100);
  ds.attachStore(&store);
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  ds.set(mac, 1, 0);
  g_growing = &ds;
  g_takes = 0;
  g_semaphoreTakeHook = growOnSecondTake;
  const std::string json = ds.asJson();
  g_semaphoreTakeHook = nullptr;
  const std::string expected = "{\"11:22:33:44:55:66\":1,\"C4:DD:57:EB:64:60\":4}";
  TEST_ASSERT_EQUAL(expected.size(), json.size());
  TEST_ASSERT_TRUE(json == expected);
}

// A heap that cannot spare the JSON's block gets no JSON.
void test_the_json_waits_for_the_heap() {
  InMemoryConfigStore store;
  DispositionStore ds(100);
  ds.attachStore(&store);
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  ds.set(mac, 1, 0);
  g_mock_largest_free_block = kHeapHeadroom;
  const std::string json = ds.asJson();
  g_mock_largest_free_block = 1 << 20;
  TEST_ASSERT_TRUE(json.empty());
}

constexpr uint64_t kMinMs = 60000;
constexpr uint64_t kHourMs = 60 * kMinMs;
constexpr uint8_t kMeetEveryH = 12;

static std::array<uint8_t, 6> peerMac(unsigned i, uint8_t top = 0x20) {
  return {top, 0, 0, 0, static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
}

static void reload(DispositionStore& into, InMemoryConfigStore& backing, uint64_t nowMs = 0) {
  into.attachStore(&backing);
  into.load(nowMs);
}

static bool samePeer(const PeerRecord& a, const PeerRecord& b) {
  return a.disp == b.disp && a.score == b.score && a.meets == b.meets &&
         a.meetAgeH == b.meetAgeH && a.unseenMin == b.unseenMin;
}

// A format 1 blob as releases up to 1.3.3 wrote it.
static void writeFormat1(InMemoryConfigStore& backing, const std::vector<std::pair<std::array<uint8_t, 6>, uint8_t>>& rows) {
  std::string blob(1, static_cast<char>(DispositionStore::kLegacyBlobFormat));
  for (const auto& r : rows) {
    blob.append(reinterpret_cast<const char*>(r.first.data()), 6);
    blob.push_back(static_cast<char>(r.second));
  }
  backing.writeBlob(DispositionStore::kBlobKey, blob.data(), blob.size());
}

// Every field of every row, at kMax rows, survives a flush and a reload, and
// the blob is the format 2 ceiling.
void test_v2_round_trip_at_kmax() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    const auto m = peerMac(i);
    ds.set(m.data(), nonNeutral(i), 0);
    ds.nudge(m.data(), static_cast<int16_t>(static_cast<int>(i % 200) - 100), 0, 0);
    for (unsigned k = 0; k < i % 7; ++k) {
      ds.meet(m.data(), 0, 0);
    }
  }
  // Ages differ per row: row i last seen i minutes before the flush.
  const uint64_t flushAt = 200 * kMinMs;
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    ds.markSeen(peerMac(i).data(), flushAt - i * kMinMs);
  }
  for (unsigned h = 0; h < 3; ++h) ds.tickHour(flushAt);
  ds.flushNow(flushAt);
  std::string blob = backing.read(DispositionStore::kBlobKey, "");
  TEST_ASSERT_EQUAL(DispositionStore::kMaxBlobLen, blob.size());
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kBlobFormat, blob[0]);
  DispositionStore back(100);
  reload(back, backing);
  const uint64_t savedAt = flushAt;
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    const auto m = peerMac(i);
    PeerRecord want = ds.peer(m.data(), savedAt);
    PeerRecord got = back.peer(m.data(), 0);
    TEST_ASSERT_TRUE_MESSAGE(samePeer(want, got), "row differs after reload");
  }
  // The fixture is not degenerate: scores, meets and ages all vary.
  TEST_ASSERT_NOT_EQUAL(back.peer(peerMac(1).data(), 0).score, back.peer(peerMac(2).data(), 0).score);
  TEST_ASSERT_NOT_EQUAL(back.peer(peerMac(1).data(), 0).meets, back.peer(peerMac(2).data(), 0).meets);
  TEST_ASSERT_NOT_EQUAL(back.peer(peerMac(1).data(), 0).unseenMin, back.peer(peerMac(2).data(), 0).unseenMin);
}

// A format 1 blob loads every disposition with score 0, meets 0, unseen 0 and
// a next meeting that counts, and writes nothing until the first flush, which
// writes format 2.
void test_format1_loads_and_rides_the_first_flush() {
  InMemoryConfigStore backing;
  writeFormat1(backing, {{peerMac(7), 1}, {peerMac(3), 5}, {peerMac(5), 3}});
  const size_t writesBefore = backing.writeCount;
  DispositionStore ds(100);
  reload(ds, backing, 5 * kHourMs);
  TEST_ASSERT_EQUAL(writesBefore, backing.writeCount);
  TEST_ASSERT_TRUE(ds.engineEnabled());
  for (auto [i, d] : {std::pair<unsigned, uint8_t>{7, 1}, {3, 5}, {5, 3}}) {
    const PeerRecord r = ds.peer(peerMac(i).data(), 5 * kHourMs);
    TEST_ASSERT_EQUAL_UINT8(d, r.disp);
    TEST_ASSERT_EQUAL_INT8(0, r.score);
    TEST_ASSERT_EQUAL_UINT8(0, r.meets);
    TEST_ASSERT_EQUAL_UINT16(0, r.unseenMin);
  }
  TEST_ASSERT_TRUE(ds.meet(peerMac(7).data(), kMeetEveryH, 5 * kHourMs));
  ds.setEngineEnabled(false, 5 * kHourMs);
  ds.flushNow(5 * kHourMs);
  TEST_ASSERT_EQUAL_UINT8(DispositionStore::kBlobFormat,
                          backing.read(DispositionStore::kBlobKey, "")[0]);
}

// A reset at every write of the first flush over a format 1 blob, and of the
// load's move of a legacy JSON key, keeps every disposition: the old copy,
// or the new one with the edit.
void test_migration_interrupted_at_every_write_keeps_every_disposition() {
  const std::vector<std::pair<std::array<uint8_t, 6>, uint8_t>> rows = {
      {peerMac(1), 1}, {peerMac(2), 2}, {peerMac(4), 4}, {peerMac(9), 5}};
  size_t completed = 0;
  for (size_t crashAt = 0; crashAt < 6; ++crashAt) {
    InMemoryConfigStore backing;
    writeFormat1(backing, rows);
    DispositionStore ds(100);
    reload(ds, backing);
    ds.set(peerMac(1).data(), 2, 0);
    backing.writesBeforeCrash = crashAt;
    ds.flushNow(0);
    backing.writesBeforeCrash = SIZE_MAX;
    DispositionStore back(100);
    reload(back, backing);
    for (const auto& r : rows) {
      const uint8_t d = back.peer(r.first.data(), 0).disp;
      if (r.first == peerMac(1)) {
        TEST_ASSERT_TRUE(d == 1 || d == 2);
      } else {
        TEST_ASSERT_EQUAL_UINT8(r.second, d);
      }
    }
    if (backing.read(DispositionStore::kBlobKey, "")[0] == DispositionStore::kBlobFormat) ++completed;
  }
  TEST_ASSERT_TRUE(completed > 0);
  for (size_t crashAt = 0; crashAt < 6; ++crashAt) {
    InMemoryConfigStore backing;
    backing.write("dispositions", "{\"20:00:00:00:00:01\":1,\"20:00:00:00:00:04\":4}");
    backing.writesBeforeCrash = crashAt;
    DispositionStore ds(100);
    reload(ds, backing);
    backing.writesBeforeCrash = SIZE_MAX;
    DispositionStore back(100);
    reload(back, backing);
    TEST_ASSERT_EQUAL_UINT8(1, back.peer(peerMac(1).data(), 0).disp);
    TEST_ASSERT_EQUAL_UINT8(4, back.peer(peerMac(4).data(), 0).disp);
  }
}

// A store whose read-back differs from what was written fails the flush:
// the legacy JSON stays and the rows stay dirty for the retry.
class CorruptingStore : public InMemoryConfigStore {
 public:
  bool corrupt = true;
  size_t readBlob(const char* key, void* out, size_t cap) override {
    const size_t n = InMemoryConfigStore::readBlob(key, out, cap);
    if (corrupt && n > 3 && std::strcmp(key, DispositionStore::kBlobKey) == 0) {
      static_cast<uint8_t*>(out)[n - 1] ^= 0xFF;
    }
    return n;
  }
};

void test_a_blob_that_reads_back_wrong_keeps_the_json() {
  CorruptingStore backing;
  backing.write("dispositions", "{\"20:00:00:00:00:01\":1}");
  DispositionStore ds(100);
  ds.attachStore(&backing);
  ds.load(0);
  TEST_ASSERT_TRUE(backing.has("dispositions"));
  backing.corrupt = false;
  ds.maybeFlush(10000);
  TEST_ASSERT_FALSE(backing.has("dispositions"));
  DispositionStore back(100);
  back.attachStore(&backing);
  back.load(0);
  TEST_ASSERT_EQUAL_UINT8(1, back.peer(peerMac(1).data(), 0).disp);
}

// Meetings count only meetEveryH on-hours apart, and the age survives a reload.
void test_meetings_count_once_per_window_across_a_reload() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  const auto m = peerMac(1);
  TEST_ASSERT_TRUE(ds.meet(m.data(), kMeetEveryH, 0));
  ds.tickHour(kHourMs);
  TEST_ASSERT_FALSE(ds.meet(m.data(), kMeetEveryH, kHourMs));  // an hour apart
  TEST_ASSERT_EQUAL_UINT8(1, ds.peer(m.data(), kHourMs).meets);
  for (unsigned h = 2; h <= 6; ++h) ds.tickHour(h * kHourMs);
  ds.maybeFlush(6 * kHourMs);
  DispositionStore back(100);
  reload(back, backing);
  TEST_ASSERT_EQUAL_UINT8(6, back.peer(m.data(), 0).meetAgeH);
  for (unsigned h = 1; h <= 5; ++h) back.tickHour(h * kHourMs);
  TEST_ASSERT_FALSE(back.meet(m.data(), kMeetEveryH, 5 * kHourMs));  // 11 on-hours
  back.tickHour(6 * kHourMs);
  TEST_ASSERT_TRUE(back.meet(m.data(), kMeetEveryH, 6 * kHourMs));  // 12 on-hours
  TEST_ASSERT_EQUAL_UINT8(2, back.peer(m.data(), 6 * kHourMs).meets);
  TEST_ASSERT_EQUAL_UINT8(0, back.peer(m.data(), 6 * kHourMs).meetAgeH);
}

static void fillStrong(DispositionStore& ds, unsigned n, uint8_t top) {
  for (unsigned i = 0; i < n; ++i) ds.set(peerMac(i, top).data(), 4, 0);
}

// At capacity the weakest row goes, whatever its MAC: strong low-MAC rows
// stay and one weak high-MAC row is replaced.
void test_eviction_takes_the_weakest_not_the_lowest_mac() {
  DispositionStore ds(100);
  fillStrong(ds, DispositionStore::kMax - 1, 0x10);
  const auto weak = peerMac(0, 0xF0);
  ds.nudge(weak.data(), 5, 0, 0);
  const auto newcomer = peerMac(1, 0x80);
  ds.set(newcomer.data(), 2, 0);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(weak.data(), 0).score);  // gone
  TEST_ASSERT_EQUAL_UINT8(2, ds.peer(newcomer.data(), 0).disp);
  for (unsigned i = 0; i < DispositionStore::kMax - 1; ++i) {
    TEST_ASSERT_EQUAL_UINT8(4, ds.peer(peerMac(i, 0x10).data(), 0).disp);
  }
}

// A regular (meets 10) outlasts a stranger of the same score.
void test_a_regular_outlasts_an_equal_score_stranger() {
  DispositionStore ds(100);
  fillStrong(ds, DispositionStore::kMax - 2, 0x10);
  const auto regular = peerMac(0, 0x30);
  const auto stranger = peerMac(0, 0xF0);
  for (unsigned k = 0; k < 10; ++k) ds.meet(regular.data(), 0, 0);
  ds.meet(stranger.data(), 0, 0);
  ds.nudge(regular.data(), 40, 0, 0);
  ds.nudge(stranger.data(), 40, 0, 0);
  ds.set(peerMac(9, 0x80).data(), 5, 0);
  TEST_ASSERT_EQUAL_UINT8(10, ds.peer(regular.data(), 0).meets);
  TEST_ASSERT_EQUAL_UINT8(0, ds.peer(stranger.data(), 0).meets);
}

// A newcomer weaker than or as strong as every present row is not admitted;
// a stronger one is, and an equal one takes an absent row's place.
void test_newcomer_admission() {
  DispositionStore ds(100);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) ds.nudge(peerMac(i).data(), 20, 0, 0);
  const auto weaker = peerMac(1, 0x90);
  ds.nudge(weaker.data(), 10, 0, kMinMs);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(weaker.data(), kMinMs).score);
  const auto equal = peerMac(2, 0x90);
  ds.nudge(equal.data(), 20, 0, kMinMs);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(equal.data(), kMinMs).score);
  ds.nudge(equal.data(), 20, 0, 2 * kHourMs);  // every row now absent
  TEST_ASSERT_EQUAL_INT8(20, ds.peer(equal.data(), 2 * kHourMs).score);
  const auto stronger = peerMac(3, 0x90);
  ds.nudge(stronger.data(), 21, 0, 2 * kHourMs);
  TEST_ASSERT_EQUAL_INT8(21, ds.peer(stronger.data(), 2 * kHourMs).score);
  size_t rows = 0;
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    if (ds.peer(peerMac(i).data(), 0).score != 0) ++rows;
  }
  TEST_ASSERT_EQUAL(DispositionStore::kMax - 2, rows);
}

// 500 distinct strangers met and nudged leave every regular and every
// non-neutral row in place, the rows within kMax, and allocate nothing.
void test_eviction_with_500_distinct_peers() {
  DispositionStore ds(100);
  for (unsigned i = 0; i < 40; ++i) ds.set(peerMac(i, 0x10).data(), 1 + (i % 2) * 4, 0);
  for (unsigned i = 0; i < 30; ++i) {
    for (unsigned k = 0; k < 3; ++k) ds.meet(peerMac(i, 0x30).data(), 0, 0);
  }
  g_allocations = 0;
  for (unsigned i = 0; i < 500; ++i) {
    const auto m = peerMac(i, 0x50);
    const uint64_t t = i * kMinMs;
    ds.meet(m.data(), kMeetEveryH, t);
    ds.nudge(m.data(), static_cast<int16_t>(i % 23), 0, t);
    ds.markSeen(m.data(), t);
  }
  TEST_ASSERT_EQUAL(0, g_allocations);
  TEST_ASSERT_EQUAL(DispositionStore::kMax, ds.rowCapacity());
  for (unsigned i = 0; i < 40; ++i) {
    TEST_ASSERT_EQUAL_UINT8(1 + (i % 2) * 4, ds.peer(peerMac(i, 0x10).data(), 0).disp);
  }
  for (unsigned i = 0; i < 30; ++i) {
    TEST_ASSERT_EQUAL_UINT8(3, ds.peer(peerMac(i, 0x30).data(), 0).meets);
  }
  size_t strangers = 0;
  for (unsigned i = 0; i < 500; ++i) {
    const PeerRecord r = ds.peer(peerMac(i, 0x50).data(), 0);
    if (r.meets || r.score) ++strangers;
  }
  TEST_ASSERT_EQUAL(DispositionStore::kMax - 70, strangers);
}

// The score steps the disposition at +-127 and restarts at 0; at 5 and 1 it
// clamps instead.
void test_nudge_steps_and_clamps() {
  DispositionStore ds(100);
  const auto m = peerMac(1);
  NudgeResult r = ds.nudge(m.data(), 126, 0, 0);
  TEST_ASSERT_FALSE(r.stepped);
  r = ds.nudge(m.data(), 1, 0, 0);
  TEST_ASSERT_TRUE(r.stepped);
  TEST_ASSERT_EQUAL_UINT8(3, r.from);
  TEST_ASSERT_EQUAL_UINT8(4, r.to);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(m.data(), 0).score);
  ds.nudge(m.data(), 127, 0, 0);
  r = ds.nudge(m.data(), 127, 0, 0);
  TEST_ASSERT_FALSE(r.stepped);
  TEST_ASSERT_EQUAL_UINT8(5, ds.peer(m.data(), 0).disp);
  TEST_ASSERT_EQUAL_INT8(127, ds.peer(m.data(), 0).score);
  for (int k = 0; k < 5; ++k) ds.nudge(m.data(), -127, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(1, ds.peer(m.data(), 0).disp);
  r = ds.nudge(m.data(), -127, 0, 0);
  TEST_ASSERT_FALSE(r.stepped);
  TEST_ASSERT_EQUAL_INT8(-127, ds.peer(m.data(), 0).score);
}

// Familiarity alone takes a peer to Fond and no further; other deltas still
// move a Fond peer up.
void test_familiarity_alone_stops_at_fond() {
  DispositionStore ds(100);
  const auto m = peerMac(1);
  for (int k = 0; k < 100; ++k) ds.nudge(m.data(), 0, 48, 0);
  TEST_ASSERT_EQUAL_UINT8(4, ds.peer(m.data(), 0).disp);
  ds.nudge(m.data(), 100, 48, 0);
  TEST_ASSERT_EQUAL_UINT8(4, ds.peer(m.data(), 0).disp);
  TEST_ASSERT_EQUAL_INT8(100, ds.peer(m.data(), 0).score);
  ds.nudge(m.data(), 27, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(5, ds.peer(m.data(), 0).disp);
}

// The reserved rows never move or grow over mixed user, engine and load
// traffic.
void test_row_storage_is_fixed() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  const size_t cap = ds.rowCapacity();
  const void* data = ds.rowStorage();
  TEST_ASSERT_EQUAL(DispositionStore::kMax, cap);
  uint32_t seed = 7;
  auto next = [&] { return seed = seed * 1103515245u + 12345u; };
  char json[64];
  for (unsigned i = 0; i < 1000; ++i) {
    const auto m = peerMac(next() % 300);
    switch (next() % 5) {
      case 0:
        ds.set(m.data(), 1 + next() % 5, i);
        break;
      case 1: {
        char key[18];
        formatBdAddr(m.data(), key);
        std::snprintf(json, sizeof(json), "{\"%s\":%u}", key, 1 + next() % 5);
        ds.setFromJson(json, std::strlen(json), i);
        break;
      }
      case 2:
        ds.flushNow(i);
        ds.load(i);
        break;
      case 3:
        ds.nudge(m.data(), static_cast<int16_t>(next() % 255) - 127, 0, i);
        break;
      default:
        ds.meet(m.data(), 0, i);
    }
    TEST_ASSERT_EQUAL(cap, ds.rowCapacity());
    TEST_ASSERT_TRUE(data == ds.rowStorage());
  }
}

// An engine nudging every second for 30 minutes writes at most once per 10
// minutes, and a user edit in the middle still lands 5 s after it.
void test_engine_flush_floor_and_user_debounce() {
  InMemoryConfigStore backing;
  DispositionStore ds(5000);
  ds.attachStore(&backing);
  const auto m = peerMac(1);
  const uint64_t userAt = 15 * kMinMs;
  uint64_t userLanded = 0;
  for (uint64_t t = 0; t <= 30 * kMinMs; t += 1000) {
    ds.nudge(m.data(), (t / 1000) % 2 ? 3 : -2, 0, t);
    if (t == userAt) ds.set(peerMac(2).data(), 5, t);
    const size_t before = backing.writeCount;
    ds.maybeFlush(t);
    if (t > userAt && !userLanded && backing.writeCount > before) userLanded = t;
  }
  TEST_ASSERT_EQUAL(userAt + 5000, userLanded);
  TEST_ASSERT_TRUE(backing.writeCount <= 3);
  InMemoryConfigStore alone;
  DispositionStore engineOnly(5000);
  engineOnly.attachStore(&alone);
  for (uint64_t t = 0; t <= 30 * kMinMs; t += 1000) {
    engineOnly.nudge(m.data(), (t / 1000) % 2 ? 3 : -2, 0, t);
    engineOnly.maybeFlush(t);
  }
  TEST_ASSERT_EQUAL(3, alone.writeCount);
}

// Age-only changes flush at most once an hour, so a power cut loses at most
// an hour of age.
void test_age_flush_floor() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  const auto m = peerMac(1);
  ds.set(m.data(), 5, 0);
  ds.meet(m.data(), 0, 0);
  ds.maybeFlush(1000);
  TEST_ASSERT_EQUAL(1, backing.writeCount);
  uint64_t lastWrite = 1000;
  for (uint64_t t = kMinMs; t <= 3 * kHourMs + 2 * kMinMs; t += kMinMs) {
    if (t % kHourMs == 0) ds.tickHour(t);
    ds.markSeen(m.data(), t - kMinMs / 2);
    const size_t before = backing.writeCount;
    ds.maybeFlush(t);
    if (backing.writeCount > before) {
      TEST_ASSERT_TRUE(t - lastWrite >= kHourMs);
      TEST_ASSERT_TRUE(t - lastWrite <= kHourMs + kMinMs);
      lastWrite = t;
    }
  }
  TEST_ASSERT_EQUAL(4, backing.writeCount);  // the edit, then one an hour
  DispositionStore back(100);
  reload(back, backing);
  TEST_ASSERT_EQUAL_UINT8(3, back.peer(m.data(), 0).meetAgeH);
}

// The boot prunes neutral rows unseen for kPruneMin and keeps a Smitten one
// as old, writing nothing.
void test_boot_prunes_aged_neutral_rows_only() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  const auto oldNeutral = peerMac(1);
  const auto youngNeutral = peerMac(2);
  const auto oldSmitten = peerMac(3);
  ds.nudge(oldNeutral.data(), 50, 0, 0);
  ds.nudge(youngNeutral.data(), 50, 0, 0);
  ds.set(oldSmitten.data(), 5, 0);
  const uint64_t at = (DispositionStore::kPruneMin + 1) * kMinMs;
  ds.markSeen(youngNeutral.data(), at - (DispositionStore::kPruneMin - 1) * kMinMs);
  ds.flushNow(at);
  const size_t writes = backing.writeCount;
  DispositionStore back(100);
  reload(back, backing);
  TEST_ASSERT_EQUAL(writes, backing.writeCount);
  TEST_ASSERT_EQUAL_INT8(0, back.peer(oldNeutral.data(), 0).score);
  TEST_ASSERT_EQUAL_INT8(50, back.peer(youngNeutral.data(), 0).score);
  TEST_ASSERT_EQUAL_UINT8(5, back.peer(oldSmitten.data(), 0).disp);
  TEST_ASSERT_EQUAL_UINT16(DispositionStore::kPruneMin + 1, back.peer(oldSmitten.data(), 0).unseenMin);
}

// A merge keeps unlisted rows, keeps an equal row's score, and resets a
// changed row's score.
void test_merge_keeps_unlisted_and_equal_rows() {
  DispositionStore ds(100);
  ds.set(peerMac(1).data(), 4, 0);
  ds.nudge(peerMac(1).data(), 30, 0, 0);
  ds.set(peerMac(2).data(), 2, 0);
  ds.nudge(peerMac(2).data(), 30, 0, 0);
  ds.set(peerMac(3).data(), 5, 0);
  const char* json = "{\"20:00:00:00:00:01\":4,\"20:00:00:00:00:02\":1}";
  TEST_ASSERT_TRUE(ds.setFromJson(json, std::strlen(json), 0));
  TEST_ASSERT_EQUAL_INT8(30, ds.peer(peerMac(1).data(), 0).score);
  TEST_ASSERT_EQUAL_UINT8(1, ds.peer(peerMac(2).data(), 0).disp);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(peerMac(2).data(), 0).score);
  TEST_ASSERT_EQUAL_UINT8(5, ds.peer(peerMac(3).data(), 0).disp);
}

// The master switch round-trips; while off no engine or age call changes a row.
void test_master_switch_round_trips_and_gates_the_engine() {
  InMemoryConfigStore backing;
  DispositionStore ds(100);
  ds.attachStore(&backing);
  const auto m = peerMac(1);
  ds.nudge(m.data(), 10, 0, 0);
  ds.meet(m.data(), 0, 0);
  ds.setEngineEnabled(false, 0);
  ds.flushNow(0);
  DispositionStore back(100);
  reload(back, backing);
  TEST_ASSERT_FALSE(back.engineEnabled());
  const PeerRecord before = back.peer(m.data(), 2 * kHourMs);
  back.nudge(m.data(), 100, 100, kHourMs);
  back.meet(m.data(), 0, kHourMs);
  back.meet(peerMac(2).data(), 0, kHourMs);
  back.markSeen(m.data(), 2 * kHourMs);
  back.tickHour(2 * kHourMs);
  back.decayTowardZero(8, 2 * kHourMs);
  TEST_ASSERT_TRUE(samePeer(before, back.peer(m.data(), 2 * kHourMs)));
  TEST_ASSERT_EQUAL_UINT8(0, back.peer(peerMac(2).data(), 0).meets);
  const size_t writes = backing.writeCount;
  back.maybeFlush(10 * kHourMs);
  TEST_ASSERT_EQUAL(writes, backing.writeCount);
  back.setEngineEnabled(true, 10 * kHourMs);
  back.flushNow(10 * kHourMs);
  DispositionStore again(100);
  reload(again, backing);
  TEST_ASSERT_TRUE(again.engineEnabled());
}

// Absence decays only rows unseen an hour or more, toward 0, never past it,
// and never moves a disposition.
void test_absence_decays_scores_of_absent_rows() {
  DispositionStore ds(100);
  const auto absent = peerMac(1);
  const auto present = peerMac(2);
  const auto negative = peerMac(3);
  ds.nudge(absent.data(), 5, 0, 0);
  ds.nudge(present.data(), 20, 0, 0);
  ds.nudge(negative.data(), -20, 0, 0);
  ds.markSeen(present.data(), 2 * kHourMs - kMinMs);
  ds.decayTowardZero(8, 2 * kHourMs);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(absent.data(), 0).score);
  TEST_ASSERT_EQUAL_INT8(20, ds.peer(present.data(), 0).score);
  TEST_ASSERT_EQUAL_INT8(-12, ds.peer(negative.data(), 0).score);
  TEST_ASSERT_EQUAL_UINT8(3, ds.peer(negative.data(), 0).disp);
}

// A user edit gets a row even when every row is stronger than it.
void test_a_user_edit_always_gets_a_row() {
  DispositionStore ds(100);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) ds.set(peerMac(i).data(), 5, 0);
  const auto m = peerMac(1, 0x90);
  ds.set(m.data(), 4, 0);
  TEST_ASSERT_EQUAL_UINT8(4, ds.peer(m.data(), 0).disp);
}

// Among equally weak rows the one unseen longest goes.
void test_eviction_ties_go_to_the_longest_unseen() {
  DispositionStore ds(100);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) ds.nudge(peerMac(i).data(), 20, 0, 0);
  for (unsigned i = 0; i < DispositionStore::kMax; ++i) {
    if (i != 57) ds.markSeen(peerMac(i).data(), 30 * kMinMs);
  }
  ds.nudge(peerMac(1, 0x90).data(), 21, 0, 30 * kMinMs);
  TEST_ASSERT_EQUAL_INT8(0, ds.peer(peerMac(57).data(), 0).score);
  TEST_ASSERT_EQUAL_INT8(20, ds.peer(peerMac(0).data(), 0).score);
  TEST_ASSERT_EQUAL_INT8(21, ds.peer(peerMac(1, 0x90).data(), 0).score);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unknown_peer_returns_default);
  RUN_TEST(test_keyed_by_mac);
  RUN_TEST(test_set_get_and_clamp);
  RUN_TEST(test_set_from_json_filters_invalid_keys);
  RUN_TEST(test_debounced_persist_and_reload_round_trip);
  RUN_TEST(test_flush_now_writes_immediately);
  RUN_TEST(test_asjson_is_sorted_canonical_colon_hex);
  RUN_TEST(test_loads_existing_persisted_blob);
  RUN_TEST(test_failed_write_backs_off_then_lands);
  RUN_TEST(test_legacy_json_migrates_on_first_flush);
  RUN_TEST(test_full_store_blob_round_trip);
  RUN_TEST(test_legacy_json_migrates_at_load_blob_first);
  RUN_TEST(test_json_written_after_a_downgrade_wins);
  RUN_TEST(test_asjson_sized_to_the_entries);
  RUN_TEST(test_corrupt_or_empty_json_keeps_the_blob);
  RUN_TEST(test_full_store_matches_arduinojson_bytes);
  RUN_TEST(test_empty_store_serializes_empty_object);
  RUN_TEST(test_a_failed_json_allocation_gives_the_lock_back);
  RUN_TEST(test_an_entry_added_between_the_locks_is_included);
  RUN_TEST(test_the_json_waits_for_the_heap);
  RUN_TEST(test_v2_round_trip_at_kmax);
  RUN_TEST(test_format1_loads_and_rides_the_first_flush);
  RUN_TEST(test_migration_interrupted_at_every_write_keeps_every_disposition);
  RUN_TEST(test_a_blob_that_reads_back_wrong_keeps_the_json);
  RUN_TEST(test_meetings_count_once_per_window_across_a_reload);
  RUN_TEST(test_eviction_takes_the_weakest_not_the_lowest_mac);
  RUN_TEST(test_a_regular_outlasts_an_equal_score_stranger);
  RUN_TEST(test_newcomer_admission);
  RUN_TEST(test_eviction_with_500_distinct_peers);
  RUN_TEST(test_nudge_steps_and_clamps);
  RUN_TEST(test_familiarity_alone_stops_at_fond);
  RUN_TEST(test_row_storage_is_fixed);
  RUN_TEST(test_engine_flush_floor_and_user_debounce);
  RUN_TEST(test_age_flush_floor);
  RUN_TEST(test_boot_prunes_aged_neutral_rows_only);
  RUN_TEST(test_merge_keeps_unlisted_and_equal_rows);
  RUN_TEST(test_master_switch_round_trips_and_gates_the_engine);
  RUN_TEST(test_absence_decays_scores_of_absent_rows);
  RUN_TEST(test_a_user_edit_always_gets_a_row);
  RUN_TEST(test_eviction_ties_go_to_the_longest_unseen);
  return UNITY_END();
}
