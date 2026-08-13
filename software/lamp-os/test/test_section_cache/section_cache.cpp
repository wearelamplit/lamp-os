// Native-host unit tests for the per-section JSON cache helper.
//
// Context: audit finding #6 + the section-read parts of #7 flagged two
// problems on the BLE GATT read path for CHAR_*_SECTION characteristics:
//
//   (a) `LampSectionCallback::onRead` (etc.) called `Config::asLampJson()`
//       on every read, building a fresh JsonDocument and walking the
//       colors / knockoutPixels vectors per read.
//   (b) Those vectors are mutated from Core 1's loop drain. A concurrent
//       BLE read on Core 0 walking a vector that's being reallocated on
//       Core 1 dereferences freed memory.
//
// Fix shape: a per-section cached std::string + dirty bool on Config.
// Core 1 mutations call invalidateXSection() AFTER mutating; the cached
// accessor rebuilds via the existing asXJson() builder on first read or
// after invalidation. ble_control::tick() (also Core 1) pushes the
// cached bytes into NimBLE via c->setValue() — NimBLE copies into its
// own internal value buffer, so subsequent reads return that copy
// without re-entering Config at all.
//
// Following test_disposition_debounce / test_color / test_fade convention:
// the cache helper is re-declared inline here as a tiny SectionCache
// struct so the native test doesn't try to link against `Config` (which
// transitively pulls in Arduino / Preferences / NimBLE). The shape
// mirrors the (string, dirty) member-pair pattern in
// src/config/config.{hpp,cpp} — if you change the cache shape there,
// mirror here.

#include <unity.h>

#include <cstdint>
#include <functional>
#include <new>
#include <string>

namespace lamp {

// Mirror of the in-Config cache shape. One per section.
// Invariant: getOrRebuild() returns the cached string. If `dirty`, it
// invokes the rebuilder, stores its result, clears dirty, returns the
// stored ref. Subsequent calls with no markDirty() in between are O(1)
// and DO NOT re-invoke the rebuilder.
struct SectionCache {
  std::string value;
  bool dirty = true;  // defaults: first read computes.

  void markDirty() { dirty = true; }

  // Returns a const ref to the cached value. If dirty, calls rebuilder()
  // to produce a fresh string, stores it, clears the dirty flag. A rebuild
  // that throws (bad_alloc under a fragmented heap) keeps the last-good
  // value and leaves dirty set so a later call retries.
  const std::string& getOrRebuild(
      const std::function<std::string()>& rebuilder) {
    if (dirty) {
      try {
        value = rebuilder();
        dirty = false;
      } catch (const std::bad_alloc&) {
      }
    }
    return value;
  }
};

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

// Helper: a rebuilder that counts how many times it's called and emits a
// deterministic string. Lets us assert "rebuild ran exactly N times".
struct CountingRebuilder {
  int calls = 0;
  std::string payload = "v1";
  std::string operator()() {
    ++calls;
    return payload;
  }
};

void test_first_read_rebuilds_and_clears_dirty() {
  // Fresh cache is dirty (so initial Config state always computes the
  // value on the first BLE read after boot).
  lamp::SectionCache c;
  CountingRebuilder rb;
  TEST_ASSERT_TRUE(c.dirty);

  const std::string& v = c.getOrRebuild([&] { return rb(); });
  TEST_ASSERT_EQUAL_INT(1, rb.calls);
  TEST_ASSERT_EQUAL_STRING("v1", v.c_str());
  TEST_ASSERT_FALSE(c.dirty);
}

void test_subsequent_reads_skip_rebuilder() {
  // The big audit win: BLE reads that hit a clean cache never re-walk
  // config vectors. Verify the rebuilder isn't invoked on read #2..N.
  lamp::SectionCache c;
  CountingRebuilder rb;

  c.getOrRebuild([&] { return rb(); });
  c.getOrRebuild([&] { return rb(); });
  c.getOrRebuild([&] { return rb(); });
  c.getOrRebuild([&] { return rb(); });

  TEST_ASSERT_EQUAL_INT(1, rb.calls);
}

void test_mark_dirty_forces_rebuild() {
  // After a Core 1 mutation calls invalidateXSection(), the next read
  // must run the rebuilder.
  lamp::SectionCache c;
  CountingRebuilder rb;

  c.getOrRebuild([&] { return rb(); });
  TEST_ASSERT_EQUAL_INT(1, rb.calls);

  rb.payload = "v2";
  c.markDirty();
  const std::string& v2 = c.getOrRebuild([&] { return rb(); });

  TEST_ASSERT_EQUAL_INT(2, rb.calls);
  TEST_ASSERT_EQUAL_STRING("v2", v2.c_str());
  TEST_ASSERT_FALSE(c.dirty);
}

void test_repeated_mark_dirty_collapses_to_single_rebuild() {
  // The user can drag a slider, hammering invalidate() many times before
  // the next read. The rebuilder must run AT MOST ONCE per read, not
  // once per markDirty.
  lamp::SectionCache c;
  CountingRebuilder rb;

  for (int i = 0; i < 50; ++i) c.markDirty();
  c.getOrRebuild([&] { return rb(); });

  TEST_ASSERT_EQUAL_INT(1, rb.calls);
}

void test_rebuild_picks_up_latest_underlying_value() {
  // The rebuilder closes over live config state. When invalidate fires
  // between the read of value A and the read of value B, the returned
  // string must reflect B — proving we're not handing out a stale ref.
  lamp::SectionCache c;
  std::string underlying = "alpha";
  auto reb = [&] { return underlying; };

  TEST_ASSERT_EQUAL_STRING("alpha", c.getOrRebuild(reb).c_str());

  underlying = "beta";
  // No markDirty yet — cache must still serve "alpha" (correctness
  // contract: invalidate must be called AFTER mutation; absent that the
  // cache is stale by design).
  TEST_ASSERT_EQUAL_STRING("alpha", c.getOrRebuild(reb).c_str());

  c.markDirty();
  TEST_ASSERT_EQUAL_STRING("beta", c.getOrRebuild(reb).c_str());
}

void test_clean_cache_returns_stable_reference() {
  // BLE callback does `c->setValue(cache.getOrRebuild(...))` —
  // NimBLE copies bytes immediately, but document the contract: while
  // the cache is clean, the returned ref points at the same underlying
  // std::string object across calls (no copy, no reallocation).
  lamp::SectionCache c;
  CountingRebuilder rb;

  const std::string& a = c.getOrRebuild([&] { return rb(); });
  const std::string& b = c.getOrRebuild([&] { return rb(); });
  TEST_ASSERT_EQUAL_PTR(&a, &b);
}

void test_independent_caches_dont_interfere() {
  // Each section gets its own SectionCache instance — invalidating one
  // section MUST NOT force a rebuild of another. Critical: lamp section
  // edits shouldn't blow the base / shade / expression caches.
  lamp::SectionCache lampCache;
  lamp::SectionCache baseCache;
  CountingRebuilder lampRb;
  CountingRebuilder baseRb;

  lampCache.getOrRebuild([&] { return lampRb(); });
  baseCache.getOrRebuild([&] { return baseRb(); });
  TEST_ASSERT_EQUAL_INT(1, lampRb.calls);
  TEST_ASSERT_EQUAL_INT(1, baseRb.calls);

  // Invalidate ONLY lamp.
  lampCache.markDirty();
  lampCache.getOrRebuild([&] { return lampRb(); });
  baseCache.getOrRebuild([&] { return baseRb(); });

  TEST_ASSERT_EQUAL_INT(2, lampRb.calls);
  TEST_ASSERT_EQUAL_INT(1, baseRb.calls);  // untouched
}

void test_throwing_rebuild_serves_empty_and_stays_dirty() {
  // A web/BLE read must never let a rebuild's bad_alloc escape. On throw the
  // cache serves its last-good value (empty on a never-built cache) and stays
  // dirty so it retries once heap recovers.
  lamp::SectionCache c;
  auto boom = []() -> std::string { throw std::bad_alloc(); };

  const std::string& v = c.getOrRebuild(boom);
  TEST_ASSERT_EQUAL_STRING("", v.c_str());
  TEST_ASSERT_TRUE(c.dirty);
}

void test_throwing_rebuild_keeps_last_good_value() {
  // After a good build, a later rebuild that throws must not clobber the
  // cached bytes — the stale value keeps serving until a build succeeds.
  lamp::SectionCache c;
  TEST_ASSERT_EQUAL_STRING("good", c.getOrRebuild([] { return std::string("good"); }).c_str());

  c.markDirty();
  auto boom = []() -> std::string { throw std::bad_alloc(); };
  TEST_ASSERT_EQUAL_STRING("good", c.getOrRebuild(boom).c_str());
  TEST_ASSERT_TRUE(c.dirty);

  TEST_ASSERT_EQUAL_STRING("fresh", c.getOrRebuild([] { return std::string("fresh"); }).c_str());
  TEST_ASSERT_FALSE(c.dirty);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_first_read_rebuilds_and_clears_dirty);
  RUN_TEST(test_subsequent_reads_skip_rebuilder);
  RUN_TEST(test_mark_dirty_forces_rebuild);
  RUN_TEST(test_repeated_mark_dirty_collapses_to_single_rebuild);
  RUN_TEST(test_rebuild_picks_up_latest_underlying_value);
  RUN_TEST(test_clean_cache_returns_stable_reference);
  RUN_TEST(test_independent_caches_dont_interfere);
  RUN_TEST(test_throwing_rebuild_serves_empty_and_stays_dirty);
  RUN_TEST(test_throwing_rebuild_keeps_last_good_value);

  return UNITY_END();
}
