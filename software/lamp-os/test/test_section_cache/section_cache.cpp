// SectionCache, the per-section JSON cache behind every BLE section read: it
// rebuilds only after an invalidate, and a section the heap cannot build or
// copy out is reported as such, never served empty, partial, or as the copy
// from before a rebuild that failed.

#include <unity.h>

#include <string>

#include "config/section_cache.hpp"

#include "../../src/config/section_cache.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"

using lamp::JsonWriter;
using lamp::kHeapHeadroom;
using lamp::SectionCache;

static constexpr size_t kPlenty = 1 << 20;

void setUp(void) { g_mock_largest_free_block = kPlenty; }
void tearDown(void) { g_mock_largest_free_block = kPlenty; }

// Builds {"name":<name>,"n":<n>} and counts its calls. The default name keeps
// the JSON past the small-string buffer, so a copy into a fresh string must
// allocate.
struct Builder {
  int calls = 0;
  std::string name = "jacko the standard lamp";
  int n = 1;
  JsonDocument operator()() {
    ++calls;
    JsonDocument doc;
    doc["name"] = name;
    doc["n"] = n;
    return doc;
  }
  std::string json() const {
    return "{\"name\":\"" + name + "\",\"n\":" + std::to_string(n) + "}";
  }
};

static void rebuild(SectionCache& c, Builder& b) {
  c.rebuild("test", [&b] { return b(); });
}

void test_copy_before_any_build_fails_empty() {
  SectionCache c;
  std::string out = "left over";
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  TEST_ASSERT_TRUE(out.empty());
}

void test_built_section_copies_out() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

void test_rebuilds_only_after_invalidate() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  rebuild(c, b);
  rebuild(c, b);
  TEST_ASSERT_EQUAL_INT(1, b.calls);
  b.n = 2;
  for (int i = 0; i < 5; ++i) c.invalidate();
  rebuild(c, b);
  TEST_ASSERT_EQUAL_INT(2, b.calls);
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

// invalidate() runs on the other core without the owner's lock. One that
// lands after the rebuild leaves the copy just built servable, and the next
// read rebuilds.
void test_invalidate_before_copy_still_copies_out() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  c.invalidate();
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
  rebuild(c, b);
  TEST_ASSERT_EQUAL_INT(2, b.calls);
}

void test_invalidate_during_rebuild_is_kept() {
  SectionCache c;
  int calls = 0;
  auto build = [&] {
    if (++calls == 1) c.invalidate();
    JsonDocument doc;
    doc["calls"] = calls;
    return doc;
  };
  c.rebuild("test", build);
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  c.rebuild("test", build);
  TEST_ASSERT_EQUAL_INT(2, calls);
}

void test_refused_first_build_fails_then_recovers() {
  SectionCache c;
  Builder b;
  g_mock_largest_free_block = kHeapHeadroom - 1;
  rebuild(c, b);
  TEST_ASSERT_EQUAL_INT(0, b.calls);
  std::string out;
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  TEST_ASSERT_TRUE(out.empty());

  g_mock_largest_free_block = kPlenty;
  rebuild(c, b);
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

// The copy from before a failed rebuild is never served: a reader gets the
// failure instead of a section that no longer matches the lamp.
void test_refused_rebuild_does_not_serve_stale() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  b.n = 2;
  c.invalidate();
  g_mock_largest_free_block = kHeapHeadroom - 1;
  rebuild(c, b);
  // Room for the copy, so only the failed rebuild can refuse it.
  std::string out;
  out.reserve(256);
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  TEST_ASSERT_TRUE(out.empty());

  g_mock_largest_free_block = kPlenty;
  rebuild(c, b);
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

void test_refused_copy_fails_empty() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  g_mock_largest_free_block = 0;
  std::string out;
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  TEST_ASSERT_TRUE(out.empty());
}

void test_copy_reuses_capacity_on_a_short_heap() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  std::string out;
  out.reserve(256);
  g_mock_largest_free_block = 0;
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

void test_release_invalidates() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  c.release();
  std::string out;
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  rebuild(c, b);
  TEST_ASSERT_EQUAL_INT(2, b.calls);
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

static void writeList(JsonWriter& w, int count) {
  w.beginArray();
  for (int i = 0; i < count; ++i) w.value(static_cast<uint32_t>(i * 37));
  w.endArray();
}

void test_exact_rebuild_copies_out() {
  SectionCache c;
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 40); });
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "list"));
  TEST_ASSERT_EQUAL_CHAR('[', out.front());
  TEST_ASSERT_EQUAL_CHAR(']', out.back());
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), ",1443]"));
}

void test_refused_exact_rebuild_does_not_serve_stale() {
  SectionCache c;
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 3); });
  c.invalidate();
  g_mock_largest_free_block = kHeapHeadroom;
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 40); });
  std::string out;
  out.reserve(256);
  TEST_ASSERT_FALSE(c.copyOut(out, "list"));
  TEST_ASSERT_TRUE(out.empty());
}

void test_exact_invalidate_during_rebuild_is_kept() {
  SectionCache c;
  int calls = 0;
  auto write = [&](JsonWriter& w) {
    if (++calls == 1) c.invalidate();
    writeList(w, 4);
  };
  c.rebuildExactly("list", write);
  std::string out;
  TEST_ASSERT_TRUE(c.copyOut(out, "list"));
  const int afterFirst = calls;
  c.rebuildExactly("list", write);
  TEST_ASSERT_GREATER_THAN_INT(afterFirst, calls);
}

// A build that throws after the section was taken for rebuilding leaves it
// invalid and still dirty, so the old copy is not served and the next read
// retries.
void test_throwing_rebuild_does_not_serve_stale() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  c.invalidate();
  c.rebuild("test", []() -> JsonDocument { throw std::bad_alloc(); });
  std::string out;
  out.reserve(256);
  TEST_ASSERT_FALSE(c.copyOut(out, "test"));
  b.n = 2;
  rebuild(c, b);
  TEST_ASSERT_TRUE(c.copyOut(out, "test"));
  TEST_ASSERT_EQUAL_STRING(b.json().c_str(), out.c_str());
}

void test_throwing_exact_rebuild_does_not_serve_stale() {
  SectionCache c;
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 3); });
  c.invalidate();
  c.rebuildExactly("list", [](JsonWriter&) { throw std::bad_alloc(); });
  std::string out;
  out.reserve(256);
  TEST_ASSERT_FALSE(c.copyOut(out, "list"));
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 5); });
  TEST_ASSERT_TRUE(c.copyOut(out, "list"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), ",148]"));
}

// The second pass writes into the exactly-sized block. The BLE page catches a
// throw that escapes the cache, so the test catches it too and checks only
// what the section serves afterward.
void test_exact_rebuild_throwing_on_second_pass_does_not_serve_stale() {
  SectionCache c;
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 3); });
  c.invalidate();
  int passes = 0;
  try {
    c.rebuildExactly("list", [&passes](JsonWriter& w) {
      if (++passes == 2) throw std::bad_alloc();
      writeList(w, 5);
    });
  } catch (const std::bad_alloc&) {
  }
  TEST_ASSERT_EQUAL_INT(2, passes);
  std::string out;
  out.reserve(256);
  TEST_ASSERT_FALSE(c.copyOut(out, "list"));
  c.rebuildExactly("list", [](JsonWriter& w) { writeList(w, 5); });
  TEST_ASSERT_TRUE(c.copyOut(out, "list"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), ",148]"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_copy_before_any_build_fails_empty);
  RUN_TEST(test_built_section_copies_out);
  RUN_TEST(test_rebuilds_only_after_invalidate);
  RUN_TEST(test_invalidate_before_copy_still_copies_out);
  RUN_TEST(test_invalidate_during_rebuild_is_kept);
  RUN_TEST(test_refused_first_build_fails_then_recovers);
  RUN_TEST(test_refused_rebuild_does_not_serve_stale);
  RUN_TEST(test_refused_copy_fails_empty);
  RUN_TEST(test_copy_reuses_capacity_on_a_short_heap);
  RUN_TEST(test_release_invalidates);
  RUN_TEST(test_exact_rebuild_copies_out);
  RUN_TEST(test_refused_exact_rebuild_does_not_serve_stale);
  RUN_TEST(test_exact_invalidate_during_rebuild_is_kept);
  RUN_TEST(test_throwing_rebuild_does_not_serve_stale);
  RUN_TEST(test_throwing_exact_rebuild_does_not_serve_stale);
  RUN_TEST(test_exact_rebuild_throwing_on_second_pass_does_not_serve_stale);
  return UNITY_END();
}
