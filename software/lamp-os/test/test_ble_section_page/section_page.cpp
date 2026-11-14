// SectionPage: a config section the heap cannot build or copy out reads as
// "!", served without allocating, never as empty bytes, which the app takes
// for a link that has not logged in. A link that has not logged in still reads
// empty, and a closed (unknown) section still ends at once.

#include <unity.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#include "components/network/ble/section_page.cpp"
#include "../replaced_new/replaced_new.hpp"

using ble_control::SectionPage;

static size_t g_allocs = 0;
static size_t g_liveBytes = 0;
static bool g_refuse = false;

// Each block carries its size in a header so a release can count it back.
constexpr size_t kHeader = alignof(std::max_align_t);

void* testAllocate(size_t n) {
  if (g_refuse) return nullptr;
  ++g_allocs;
  auto* block = static_cast<unsigned char*>(std::malloc(n + kHeader));
  if (!block) return nullptr;
  *reinterpret_cast<size_t*>(block) = n;
  g_liveBytes += n;
  return block + kHeader;
}
void testRelease(void* p) noexcept {
  auto* block = static_cast<unsigned char*>(p) - kHeader;
  g_liveBytes -= *reinterpret_cast<size_t*>(block);
  std::free(block);
}

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

static const std::string kBigJson(SectionPage::kKeptCapacity + 1, 'x');
static bool copyBig(std::string& out) {
  out.assign(kBigJson);
  return true;
}
static void serializeBig(std::string& out) { out.assign(kBigJson); }

// A large section's copy is freed by the read that ends it, so its bytes do
// not stay held between reads.
void test_end_of_large_section_frees_the_copy() {
  SectionPage page;
  const size_t before = g_liveBytes;
  page.openConfig(copyBig, 64);
  TEST_ASSERT_TRUE(g_liveBytes > before);
  drain(page);
  TEST_ASSERT_EQUAL_UINT(before, g_liveBytes);

  page.open(serializeBig, 64);
  TEST_ASSERT_TRUE(g_liveBytes > before);
  TEST_ASSERT_EQUAL_UINT(kBigJson.size(), drain(page).size());
  TEST_ASSERT_EQUAL_UINT(before, g_liveBytes);
}

static const std::string kAtThresholdJson(SectionPage::kKeptCapacity, 't');
static bool copyAtThreshold(std::string& out) {
  out.assign(kAtThresholdJson);
  return true;
}

static bool rereadsWithNoHeap(SectionPage& page, SectionPage::ConfigCopy copy) {
  g_refuse = true;
  const bool served = page.openConfig(copy, 64);
  g_refuse = false;
  drain(page);
  return served;
}

// The keep-or-free line sits at kKeptCapacity: a section that size keeps its
// buffer, so a different, smaller section re-reads into it with no heap to
// spare; one byte more frees it, and the same re-read finds no buffer.
void test_kept_buffer_threshold_and_a_smaller_section() {
  SectionPage kept;
  kept.openConfig(copyAtThreshold, 64);
  drain(kept);
  TEST_ASSERT_TRUE(rereadsWithNoHeap(kept, copyLamp));

  SectionPage freed;
  freed.openConfig(copyBig, 64);
  drain(freed);
  TEST_ASSERT_FALSE(rereadsWithNoHeap(freed, copyLamp));
}

// A small section keeps its buffer, so a re-read serves with no heap to spare
// where a fresh copy would read busy.
void test_small_section_rereads_without_allocating() {
  SectionPage page;
  page.openConfig(copyLamp, 16);
  drain(page);
  g_refuse = true;
  const bool served = page.openConfig(copyLamp, 16);
  g_refuse = false;
  TEST_ASSERT_TRUE(served);
  TEST_ASSERT_EQUAL_STRING(kLampJson.c_str(), drain(page).c_str());
}

// The read that serves the last bytes keeps them until that read is done.
void test_last_chunk_is_served_before_the_free() {
  SectionPage page;
  page.openConfig(copyLamp, static_cast<uint16_t>(kLampJson.size()));
  const std::string_view whole = page.read(true);
  TEST_ASSERT_EQUAL_STRING(kLampJson.c_str(), std::string(whole).c_str());
  TEST_ASSERT_TRUE(page.read(true).empty());
  TEST_ASSERT_TRUE(page.read(true).empty());
}

// Grown from the shorter, the longer one's buffer doubles to just under twice
// kKeptCapacity; the abandoned one's sits just over it.
static const std::string kShortJson(SectionPage::kKeptCapacity - 24, 's');
static const std::string kLongerJson(SectionPage::kKeptCapacity - 14, 'l');
static const std::string kHugeJson(2 * SectionPage::kKeptCapacity + 64, 'h');
static bool copyShort(std::string& out) {
  out.assign(kShortJson);
  return true;
}
static bool copyLonger(std::string& out) {
  out.assign(kLongerJson);
  return true;
}
static bool copyHuge(std::string& out) {
  out.assign(kHugeJson);
  return true;
}

// Small sections that grow the kept buffer past kKeptCapacity, as a string
// doubles, still keep it: the longer one re-reads with no heap to spare.
void test_small_sections_keep_a_buffer_they_grew() {
  SectionPage page;
  const size_t before = g_liveBytes;
  page.openConfig(copyShort, 64);
  drain(page);
  page.openConfig(copyLonger, 64);
  drain(page);
  TEST_ASSERT_TRUE(g_liveBytes - before > SectionPage::kKeptCapacity);
  TEST_ASSERT_TRUE(rereadsWithNoHeap(page, copyLonger));
}

// Bytes a page keeps after reading kLampJson to its end from no buffer.
static size_t smallSectionKeeps() {
  SectionPage page;
  const size_t before = g_liveBytes;
  page.openConfig(copyLamp, 64);
  drain(page);
  return g_liveBytes - before;
}

// A large read abandoned midway leaves its buffer; once the next small section
// ends, the page holds no more than that section keeps.
void test_small_section_end_frees_an_abandoned_large_buffer() {
  const size_t keeps = smallSectionKeeps();
  SectionPage page;
  const size_t before = g_liveBytes;
  page.openConfig(copyHuge, 64);
  page.read(true);
  page.openConfig(copyLamp, 64);
  drain(page);
  TEST_ASSERT_TRUE(g_liveBytes - before <= keeps);
}

// Abandoned sizes over kKeptCapacity, up to twice it: buffers a capacity test
// cannot tell from one small sections grew.
static const size_t kAbandonedSizes[] = {SectionPage::kKeptCapacity + 1,
                                         SectionPage::kKeptCapacity * 3 / 2 + 7,
                                         2 * SectionPage::kKeptCapacity};
static size_t g_abandonedSize = 0;
static bool copyAbandoned(std::string& out) {
  out.assign(g_abandonedSize, 'a');
  return true;
}

// Opens a section of g_abandonedSize bytes and stops after its first chunk.
static void abandonMidway(SectionPage& page, size_t size) {
  g_abandonedSize = size;
  page.openConfig(copyAbandoned, 64);
  TEST_ASSERT_EQUAL_UINT(64, page.read(true).size());
}

// The section that replaces an abandoned one over kKeptCapacity frees its
// buffer, whatever capacity that buffer has.
void test_next_open_frees_a_buffer_an_abandoned_read_left() {
  const size_t keeps = smallSectionKeeps();
  for (const size_t size : kAbandonedSizes) {
    const size_t before = g_liveBytes;

    SectionPage config;
    abandonMidway(config, size);
    config.openConfig(copyLamp, 64);
    TEST_ASSERT_EQUAL_STRING(kLampJson.c_str(), drain(config).c_str());
    TEST_ASSERT_TRUE(g_liveBytes - before <= keeps);

    const size_t beforeOther = g_liveBytes;
    SectionPage other;
    abandonMidway(other, size);
    other.open(serializeLamp, 64);
    TEST_ASSERT_EQUAL_STRING(kLampJson.c_str(), drain(other).c_str());
    TEST_ASSERT_TRUE(g_liveBytes - beforeOther <= keeps);

    static const char kCatalog[] = "{\"catalog\":[1,2,3]}";
    const size_t beforeFlash = g_liveBytes;
    SectionPage flash;
    abandonMidway(flash, size);
    flash.openStatic(std::string_view(kCatalog, sizeof(kCatalog) - 1), 64);
    TEST_ASSERT_EQUAL_UINT(beforeFlash, g_liveBytes);

    const size_t beforeUnknown = g_liveBytes;
    SectionPage unknown;
    abandonMidway(unknown, size);
    unknown.close();
    TEST_ASSERT_EQUAL_UINT(beforeUnknown, g_liveBytes);
  }
}

// An abandoned section within kKeptCapacity keeps its buffer for the next.
void test_next_open_keeps_a_small_abandoned_buffer() {
  SectionPage page;
  abandonMidway(page, SectionPage::kKeptCapacity);
  TEST_ASSERT_TRUE(rereadsWithNoHeap(page, copyLamp));
}

// A build that grows the buffer past twice kKeptCapacity, then fails.
static void serializeGrowThenThrow(std::string& out) {
  out.reserve(3 * SectionPage::kKeptCapacity);
  throw std::bad_alloc();
}

// The end of a section whose build failed frees the empty buffer it grew.
void test_failed_build_frees_the_buffer_it_grew() {
  SectionPage page;
  const size_t before = g_liveBytes;
  TEST_ASSERT_FALSE(page.open(serializeGrowThenThrow, 64));
  TEST_ASSERT_TRUE(g_liveBytes - before > 2 * SectionPage::kKeptCapacity);
  TEST_ASSERT_TRUE(drain(page).empty());
  TEST_ASSERT_EQUAL_UINT(before, g_liveBytes);
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
  RUN_TEST(test_end_of_large_section_frees_the_copy);
  RUN_TEST(test_small_section_rereads_without_allocating);
  RUN_TEST(test_kept_buffer_threshold_and_a_smaller_section);
  RUN_TEST(test_small_sections_keep_a_buffer_they_grew);
  RUN_TEST(test_small_section_end_frees_an_abandoned_large_buffer);
  RUN_TEST(test_last_chunk_is_served_before_the_free);
  RUN_TEST(test_next_open_frees_a_buffer_an_abandoned_read_left);
  RUN_TEST(test_next_open_keeps_a_small_abandoned_buffer);
  RUN_TEST(test_failed_build_frees_the_buffer_it_grew);
  return UNITY_END();
}
