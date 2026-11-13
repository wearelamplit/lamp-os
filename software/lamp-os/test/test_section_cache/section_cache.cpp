// SectionCache, the per-section JSON cache behind every BLE section read: it
// rebuilds only after an invalidate, and a section the heap cannot build or
// copy out is reported as such, never served empty, stale or partial.

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

// The last good copy is never served once the section changed: a reader gets
// the failure instead of a section that no longer matches the lamp.
void test_refused_rebuild_does_not_serve_stale() {
  SectionCache c;
  Builder b;
  rebuild(c, b);
  b.n = 2;
  c.invalidate();
  g_mock_largest_free_block = kHeapHeadroom - 1;
  rebuild(c, b);
  std::string out;
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
  TEST_ASSERT_FALSE(c.copyOut(out, "list"));
  TEST_ASSERT_TRUE(out.empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_copy_before_any_build_fails_empty);
  RUN_TEST(test_built_section_copies_out);
  RUN_TEST(test_rebuilds_only_after_invalidate);
  RUN_TEST(test_refused_first_build_fails_then_recovers);
  RUN_TEST(test_refused_rebuild_does_not_serve_stale);
  RUN_TEST(test_refused_copy_fails_empty);
  RUN_TEST(test_copy_reuses_capacity_on_a_short_heap);
  RUN_TEST(test_release_invalidates);
  RUN_TEST(test_exact_rebuild_copies_out);
  RUN_TEST(test_refused_exact_rebuild_does_not_serve_stale);
  return UNITY_END();
}
