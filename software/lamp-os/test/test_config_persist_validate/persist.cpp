// Native tests for saving a whole web document (config_codec::saveRawJson,
// which Config::persistRawJson runs): a truncated or lamp-less body is
// refused before any write, a heap that cannot spare the parse refuses before
// any allocation, and a good document saves in the stored shape.

#include <unity.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>

#include "config/config_codec.hpp"
#include "config/config_store.hpp"
#include "esp_heap_caps.h"

// Native tests don't build src/, so compile the real implementations in.
#include "../../src/util/color.cpp"
#include "../../src/config/config_codec.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../replaced_new/replaced_new.hpp"

using namespace lamp;
using config_codec::SaveResult;

// The web PUT's body limit (webapp.cpp's kMaxSettingsBody).
constexpr size_t kMaxSettingsBodyForTest = 16 * 1024;

// Heap use while a web document is saved, counted on the host: the parse
// through a counting ArduinoJson allocator, the model through operator new.
// Host slots and pools are larger than the ESP32's (64-bit pointers), so the
// figures run high.
namespace {

struct Tally {
  size_t live = 0;
  size_t peak = 0;
  size_t largest = 0;
  void add(size_t n) {
    live += n;
    peak = std::max(peak, live);
    largest = std::max(largest, n);
  }
};

Tally g_newTally;
bool g_counting = false;

class CountingAllocator : public ArduinoJson::Allocator {
 public:
  Tally tally;
  void* allocate(size_t size) override {
    void* p = std::malloc(size);
    if (p) {
      sizes_[p] = size;
      tally.add(size);
    }
    return p;
  }
  void deallocate(void* p) override {
    tally.live -= sizes_[p];
    sizes_.erase(p);
    std::free(p);
  }
  void* reallocate(void* p, size_t size) override {
    // A realloc holds both blocks at its peak.
    void* q = allocate(size);
    if (q && p) {
      std::memcpy(q, p, std::min(size, sizes_[p]));
      deallocate(p);
    }
    return q;
  }

 private:
  std::map<void*, size_t> sizes_;
};

}  // namespace

void* testAllocate(size_t size) {
  void* p = std::malloc(size + sizeof(size_t));
  if (!p) return nullptr;
  *static_cast<size_t*>(p) = size;
  if (g_counting) g_newTally.add(size);
  return static_cast<size_t*>(p) + 1;
}
void testRelease(void* p) noexcept {
  size_t* base = static_cast<size_t*>(p) - 1;
  if (g_counting) g_newTally.live -= std::min(g_newTally.live, *base);
  std::free(base);
}

void setUp(void) {
  g_mock_largest_free_block = 1 << 20;
  g_mock_free_size = 1 << 20;
}
void tearDown(void) {}

static uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

static SaveResult persist(InMemoryConfigStore& s, const char* json,
                          const std::string& kept = "") {
  return config_codec::saveRawJson(s, json, s_buf, sizeof(s_buf), kept);
}

// A lamp field as the store loads it, in the JSON the web document carries.
static std::string storedField(InMemoryConfigStore& s, const char* field) {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
  config_codec::loadConfig(s, lamp, base, shade, expressions, homeMode, s_buf, sizeof(s_buf));
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), lamp, base, shade, expressions, homeMode);
  return doc["lamp"][field].as<std::string>();
}

static std::string storedName(InMemoryConfigStore& s) { return storedField(s, "name"); }

static const char* kGood = R"({"lamp":{"name":"jacko","named":true}})";
static const char* kTruncated = R"({"lamp":{"name":"jac)";  // silent-OOM shape
static const char* kNoLampObject = R"({"base":{"px":32}})";

void test_truncated_blob_rejected_and_store_untouched() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kGood) == SaveResult::Ok);
  const size_t writes = s.writeCount;
  TEST_ASSERT_TRUE(persist(s, kTruncated) == SaveResult::Invalid);
  TEST_ASSERT_EQUAL(writes, s.writeCount);
  TEST_ASSERT_EQUAL_STRING("jacko", storedName(s).c_str());
}

void test_blob_without_lamp_object_rejected() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kNoLampObject) == SaveResult::Invalid);
  TEST_ASSERT_EQUAL(0, s.writeCount);
}

void test_valid_whole_document_saved_in_the_stored_shape() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, kGood) == SaveResult::Ok);
  TEST_ASSERT_EQUAL_STRING("jacko", storedName(s).c_str());
  TEST_ASSERT_TRUE(s.has("st0") && s.has("xn") && !s.has("cfg"));
}

void test_null_rejected() {
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, nullptr) == SaveResult::Invalid);
}

// The parse and the model need about three times the document plus the
// model's ceiling; a heap that cannot spare it refuses before allocating.
// Short of either figure the gate takes, the save is refused before any
// allocation; with both it goes ahead.
void test_low_heap_refuses_before_parsing() {
  const auto shape = config_codec::scanRawJson(kGood);
  for (int short_ : {0, 1}) {
    InMemoryConfigStore s;
    g_mock_free_size = shape.heapBytes() + kHeapHeadroom - (short_ == 0 ? 1 : 0);
    g_mock_largest_free_block = shape.largestBlock() + kHeapHeadroom - (short_ == 1 ? 1 : 0);
    TEST_ASSERT_TRUE(persist(s, kGood) == SaveResult::NoHeap);
    TEST_ASSERT_EQUAL(0, s.writeCount);
  }
  InMemoryConfigStore s;
  g_mock_free_size = shape.heapBytes() + kHeapHeadroom;
  g_mock_largest_free_block = shape.largestBlock() + kHeapHeadroom;
  TEST_ASSERT_TRUE(persist(s, kGood) == SaveResult::Ok);
}

namespace {

struct Measured {
  size_t len;
  size_t peak;     // the parse's peak plus the model's, both alive together
  size_t largest;  // the largest single block either takes
};

Measured measure(const std::string& json) {
  CountingAllocator parse;
  JsonDocument doc(&parse);
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  g_newTally = {};
  g_counting = true;
  {
    LampSettings lamp;
    BaseSettings base;
    ShadeSettings shade;
    ExpressionSettings expressions;
    HomeModeSettings homeMode;
    config_codec::fromJson(doc.as<JsonObject>(), lamp, base, shade, expressions, homeMode);
  }
  g_counting = false;
  return {json.size(), parse.tally.peak + g_newTally.peak,
          std::max(parse.tally.largest, g_newTally.largest)};
}

ExpressionConfig full(const char* type, uint8_t target) {
  ExpressionConfig e;
  e.type = type;
  e.target = target;
  for (size_t i = 0; i < kMaxExpressionColors; ++i) e.colors.push_back(Color(1, 2, 3, 4));
  for (size_t i = 0; i < kMaxExpressionParams; ++i) {
    e.parameters.set(kExpressionParamKeys[i], 0xFFFFFFFFu);
  }
  return e;
}

// The web PUT's handling, done in the one parse: the stored password stays
// when the document carries none (GET redacts it), a named lamp is marked
// named, and an over-long password or SSID is refused.
void test_web_document_fixups() {
  {
    InMemoryConfigStore unnamed;
    TEST_ASSERT_TRUE(persist(unnamed, R"({"lamp":{}})") == SaveResult::Ok);
    TEST_ASSERT_EQUAL_STRING("false", storedField(unnamed, "named").c_str());
  }
  InMemoryConfigStore s;
  TEST_ASSERT_TRUE(persist(s, R"({"lamp":{"name":"jacko"}})", "secret") == SaveResult::Ok);
  TEST_ASSERT_EQUAL_STRING("secret", storedField(s, "password").c_str());
  TEST_ASSERT_EQUAL_STRING("true", storedField(s, "named").c_str());
  TEST_ASSERT_TRUE(persist(s, R"({"lamp":{"name":"jacko","password":"new"}})", "secret") ==
                   SaveResult::Ok);
  TEST_ASSERT_EQUAL_STRING("new", storedField(s, "password").c_str());
  const size_t writes = s.writeCount;
  const std::string longPassword =
      R"({"lamp":{"password":")" + std::string(kMaxPasswordLen + 1, 'p') + R"("}})";
  TEST_ASSERT_TRUE(persist(s, longPassword.c_str()) == SaveResult::Invalid);
  const std::string longSsid = R"({"lamp":{},"homeMode":{"ssid":")" +
                               std::string(kMaxSsidLen + 1, 's') + R"("}})";
  TEST_ASSERT_TRUE(persist(s, longSsid.c_str()) == SaveResult::Invalid);
  TEST_ASSERT_EQUAL(writes, s.writeCount);
}

// A web document as the stored JSON of a lamp with `n` full expressions.
std::string document(size_t n, size_t nameLen = 8) {
  LampSettings lamp;
  lamp.name = std::string(nameLen, 'n');
  BaseSettings base;
  ShadeSettings shade;
  HomeModeSettings homeMode;
  ExpressionSettings expressions;
  for (size_t i = 0; i < n; ++i) expressions.expressions.push_back(full("pulse", 1 + i % 3));
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), lamp, base, shade, expressions, homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace

// A lamp at every cap: 21 full expressions, 4 base and 4 shade segments of 8
// colours with 16-byte names and two-digit pins, 252 knockout levels and a full disabled list.
std::string atCapsDocument() {
  LampSettings lamp;
  lamp.name = std::string(kMaxLampNameLen, 'n');
  lamp.password = std::string(kMaxPasswordLen, 'p');
  BaseSettings base;
  ShadeSettings shade;
  for (auto* segs : {&base.segments, &shade.segments}) {
    segs->clear();
    for (size_t i = 0; i < kMaxSegments; ++i) {
      SegmentSettings seg;
      seg.name = std::string(kMaxSegmentNameLen, 's');
      seg.px = 63;
      seg.pin = 33;
      seg.colors.assign(kMaxSurfaceColors, Color(255, 255, 255, 255));
      segs->push_back(seg);
    }
  }
  base.knockoutPixels.assign(252, 100);
  HomeModeSettings homeMode;
  homeMode.ssid = std::string(kMaxSsidLen, 'w');
  homeMode.disabledExpressionTypes.assign(kMaxDisabledExpressionTypes,
                                          std::string(kMaxExpressionTypeLen, 't'));
  ExpressionSettings expressions;
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    expressions.expressions.push_back(full("pulse", 1 + i % 3));
  }
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), lamp, base, shade, expressions, homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// The gate is set from these: a typical document (about 5 KB), the standard
// variant's 18 full expressions, every expression the caps allow, a lamp at
// every cap, and the 16 KB the web PUT accepts made of the smallest elements
// (the most slots per byte), of distinct short strings as values and as keys
// (the most strings per byte) and of one long string (the largest block).
void test_raw_save_heap_figures_stay_inside_the_gate() {
  const size_t maxBody = kMaxSettingsBodyForTest;
  std::vector<std::string> docs = {document(10), document(18), document(kMaxExpressions),
                                   atCapsDocument()};
  std::string slots = "{\"lamp\":{},\"x\":[";
  while (slots.size() < maxBody - 4) slots += "0,";
  slots += "0]}";
  docs.push_back(slots);
  char name[8];
  std::string values = "{\"lamp\":{},\"x\":[";
  std::string keys = "{\"lamp\":{},\"x\":{";
  for (unsigned i = 0; values.size() < maxBody - 10; ++i) {
    std::snprintf(name, sizeof(name), "%04x", i & 0xffff);
    values += std::string(i ? "," : "") + "\"" + name + "\"";
  }
  for (unsigned i = 0; keys.size() < maxBody - 12; ++i) {
    std::snprintf(name, sizeof(name), "%04x", i & 0xffff);
    keys += std::string(i ? "," : "") + "\"" + name + "\":0";
  }
  docs.push_back(values + "]}");
  docs.push_back(keys + "}}");
  docs.push_back("{\"lamp\":{\"name\":\"" + std::string(maxBody - 24, 'n') + "\"}}");
  // Just past a doubling of the string being built.
  docs.push_back("{\"lamp\":{\"name\":\"" + std::string(8193, 'n') + "\"}}");
  bool inside = true;
  for (const auto& json : docs) {
    const Measured m = measure(json);
    const auto shape = config_codec::scanRawJson(json.c_str());
    printf("[raw] %zu B, %zu tokens, %zu distinct strings, longest %zu: peak %zu of %zu, "
           "largest %zu of %zu\n",
           m.len, shape.tokens, shape.distinct, shape.longest, m.peak, shape.heapBytes(),
           m.largest, shape.largestBlock());
    inside &= m.peak <= shape.heapBytes() && m.largest <= shape.largestBlock();
  }
  TEST_ASSERT_TRUE(inside);
}

// ArduinoJson also reads single-quoted strings and unquoted keys, which the
// gate's scan would not measure: each such document is refused before the
// parse, as the web UI only sends strict JSON. The same shapes with every
// string double-quoted pass the scan.
void test_lenient_json_is_refused_before_the_parse() {
  const size_t maxBody = kMaxSettingsBodyForTest;
  char name[8];
  std::string singleValues = "{\"lamp\":{},\"x\":[";
  std::string bareKeys = "{\"lamp\":{},\"x\":{";
  for (unsigned i = 0; singleValues.size() < maxBody - 10; ++i) {
    std::snprintf(name, sizeof(name), "%04x", i & 0xffff);
    singleValues += std::string(i ? "," : "") + "'" + name + "'";
  }
  for (unsigned i = 0; bareKeys.size() < maxBody - 12; ++i) {
    std::snprintf(name, sizeof(name), "k%04x", i & 0xffff);
    bareKeys += std::string(i ? "," : "") + name + ":0";
  }
  const std::string probes[] = {
      "{\"lamp\":{\"name\":'" + std::string(maxBody - 24, 'n') + "'}}",
      "{lamp:{name:\"" + std::string(maxBody - 24, 'n') + "\"}}",
      singleValues + "]}",
      bareKeys + "}}",
      "{\"lamp\":{},\"x\":{" + std::string(maxBody - 24, '1') + ":0}}",
      "{\"lamp\":{\"name\":nullnull}}",
  };
  for (const auto& probe : probes) {
    TEST_ASSERT_FALSE(config_codec::scanRawJson(probe.c_str()).strict);
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE(persist(s, probe.c_str()) == SaveResult::Invalid);
    TEST_ASSERT_EQUAL(0, s.writeCount);
  }
  for (const char* strict :
       {R"({"lamp":{"name":"x","named":true,"brightness":-1.5e+2,"password":null}})",
        R"( { "lamp" : { } , "x" : [ 1 , "a" , false , { } ] } )"}) {
    TEST_ASSERT_TRUE(config_codec::scanRawJson(strict).strict);
  }
  TEST_ASSERT_TRUE(config_codec::scanRawJson(document(10).c_str()).strict);
}

// A parse that runs out of memory all the same is a heap failure (500), and
// any other parse error an invalid body (400).
void test_a_parse_out_of_memory_is_no_heap() {
  TEST_ASSERT_TRUE(config_codec::rawParseResult(DeserializationError::NoMemory) ==
                   SaveResult::NoHeap);
  TEST_ASSERT_TRUE(config_codec::rawParseResult(DeserializationError::InvalidInput) ==
                   SaveResult::Invalid);
  TEST_ASSERT_TRUE(config_codec::rawParseResult(DeserializationError::Ok) == SaveResult::Ok);
}

// A typical 5 KB document, and the standard variant's 18 full expressions,
// pass the gate at the runtime figures docs/dev/embedded-heap.md designs
// against (a 23.5 KB largest block in 46.5 KB free) with the web handler's
// live memory taken out first: the request body, held until the save ends.
// The handler keeps no parse of its own.
void test_typical_documents_pass_at_the_documented_heap() {
  for (size_t n : {10, 18}) {
    const std::string json = document(n);
    g_mock_largest_free_block = 23500;
    g_mock_free_size = 46500 - (json.size() + 1);
    InMemoryConfigStore s;
    TEST_ASSERT_TRUE_MESSAGE(persist(s, json.c_str()) == SaveResult::Ok,
                             std::to_string(n).c_str());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_truncated_blob_rejected_and_store_untouched);
  RUN_TEST(test_blob_without_lamp_object_rejected);
  RUN_TEST(test_valid_whole_document_saved_in_the_stored_shape);
  RUN_TEST(test_null_rejected);
  RUN_TEST(test_low_heap_refuses_before_parsing);
  RUN_TEST(test_raw_save_heap_figures_stay_inside_the_gate);
  RUN_TEST(test_typical_documents_pass_at_the_documented_heap);
  RUN_TEST(test_web_document_fixups);
  RUN_TEST(test_lenient_json_is_refused_before_the_parse);
  RUN_TEST(test_a_parse_out_of_memory_is_no_heap);
  return UNITY_END();
}
