// SectionPage: a config section the heap cannot build or copy out reads as
// "!", served without allocating, never as empty bytes, which the app takes
// for a link that has not logged in. A link that has not logged in still reads
// empty, and a closed (unknown) section still ends at once.

#include <unity.h>

#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#include "components/network/ble/section_page.cpp"
#include "../replaced_new/replaced_new.hpp"

using ble_control::SectionPage;

static size_t g_allocs = 0;
static bool g_refuse = false;

void* testAllocate(size_t n) {
  if (g_refuse) return nullptr;
  ++g_allocs;
  return std::malloc(n);
}
void testRelease(void* p) noexcept { std::free(p); }

// Longer than the small-string buffer, so a copy of it must allocate.
static const std::string kLampJson =
    R"({"name":"jacko","brightness":120,"hasPassword":true,"lampType":"standard"})";

static bool copyLamp(std::string& out) {
  out.assign(kLampJson);
  return true;
}
static bool copyRefused(std::string& out) {
  out.clear();
  return false;
}
// A failed copy that leaves bytes behind, against its contract.
static bool copyRefusedDirty(std::string& out) {
  out.assign(kLampJson, 0, 20);
  return false;
}
static void serializeLamp(std::string& out) { out.assign(kLampJson); }

// Every chunk until the empty one that ends the section.
static std::string drain(SectionPage& page, bool authed = true) {
  std::string all;
  for (int i = 0; i < 1000; ++i) {
    const std::string_view chunk = page.read(authed);
    if (chunk.empty()) return all;
    all.append(chunk);
  }
  TEST_FAIL_MESSAGE("section never ended");
  return all;
}

void setUp(void) {
  g_refuse = false;
  g_allocs = 0;
}
void tearDown(void) { g_refuse = false; }

void test_config_section_reads_whole_in_chunks() {
  SectionPage page;
  TEST_ASSERT_TRUE(page.openConfig(copyLamp, 7));
  TEST_ASSERT_EQUAL_UINT(7, page.read(true).size());
  TEST_ASSERT_EQUAL_STRING(kLampJson.substr(7).c_str(), drain(page).c_str());
}

void test_refused_copy_reads_busy() {
  SectionPage page;
  TEST_ASSERT_FALSE(page.openConfig(copyRefused, 20));
  const std::string_view chunk = page.read(true);
  TEST_ASSERT_EQUAL_UINT(1, chunk.size());
  TEST_ASSERT_EQUAL_CHAR('!', chunk[0]);
  TEST_ASSERT_TRUE(page.read(true).empty());
}

void test_busy_ignores_bytes_a_failed_copy_left() {
  SectionPage page;
  TEST_ASSERT_FALSE(page.openConfig(copyRefusedDirty, 64));
  TEST_ASSERT_EQUAL_STRING("!", drain(page).c_str());
}

void test_copy_that_throws_bad_alloc_reads_busy() {
  SectionPage page;
  g_refuse = true;
  TEST_ASSERT_FALSE(page.openConfig(copyLamp, 64));
  g_refuse = false;
  TEST_ASSERT_EQUAL_STRING("!", drain(page).c_str());
}

void test_busy_is_served_without_allocating() {
  SectionPage page;
  g_allocs = 0;
  page.openConfig(copyRefused, 64);
  const std::string_view chunk = page.read(true);
  TEST_ASSERT_EQUAL_UINT(0, g_allocs);
  TEST_ASSERT_EQUAL_PTR(SectionPage::kBusy.data(), chunk.data());
}

void test_busy_replaces_a_section_read_before() {
  SectionPage page;
  page.openConfig(copyLamp, 8);
  page.read(true);
  page.openConfig(copyRefused, 8);
  TEST_ASSERT_EQUAL_STRING("!", drain(page).c_str());
}

void test_not_logged_in_reads_empty() {
  SectionPage page;
  page.openConfig(copyLamp, 16);
  TEST_ASSERT_TRUE(page.read(false).empty());
  page.openConfig(copyRefused, 16);
  TEST_ASSERT_TRUE(page.read(false).empty());
}

void test_closed_section_reads_empty() {
  SectionPage page;
  page.openConfig(copyLamp, 16);
  page.read(true);
  page.close();
  TEST_ASSERT_TRUE(page.read(true).empty());
  SectionPage fresh;
  TEST_ASSERT_TRUE(fresh.read(true).empty());
}

void test_other_section_bad_alloc_reads_empty() {
  SectionPage page;
  g_refuse = true;
  TEST_ASSERT_FALSE(page.open(serializeLamp, 64));
  g_refuse = false;
  TEST_ASSERT_TRUE(page.read(true).empty());
}

void test_other_section_reads_whole() {
  SectionPage page;
  TEST_ASSERT_TRUE(page.open(serializeLamp, 9));
  TEST_ASSERT_EQUAL_STRING(kLampJson.c_str(), drain(page).c_str());
}

void test_static_section_serves_by_reference() {
  static const char kCatalog[] = "{\"catalog\":[1,2,3,4,5,6,7,8,9]}";
  SectionPage page;
  page.openStatic(std::string_view(kCatalog, sizeof(kCatalog) - 1), 5);
  const std::string_view first = page.read(true);
  TEST_ASSERT_EQUAL_PTR(kCatalog, first.data());
  TEST_ASSERT_EQUAL_STRING(kCatalog + 5, drain(page).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_config_section_reads_whole_in_chunks);
  RUN_TEST(test_refused_copy_reads_busy);
  RUN_TEST(test_busy_ignores_bytes_a_failed_copy_left);
  RUN_TEST(test_copy_that_throws_bad_alloc_reads_busy);
  RUN_TEST(test_busy_is_served_without_allocating);
  RUN_TEST(test_busy_replaces_a_section_read_before);
  RUN_TEST(test_not_logged_in_reads_empty);
  RUN_TEST(test_closed_section_reads_empty);
  RUN_TEST(test_other_section_bad_alloc_reads_empty);
  RUN_TEST(test_other_section_reads_whole);
  RUN_TEST(test_static_section_serves_by_reference);
  return UNITY_END();
}
