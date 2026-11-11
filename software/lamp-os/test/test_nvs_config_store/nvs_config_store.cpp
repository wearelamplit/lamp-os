// Native tests for the device store itself: NvsConfigStore compiled over a
// native NVS backed by the page model, so its blob writes take the same path
// past an out-of-memory orphan that PagedNvs's own writes do.

#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "../test_config_records/paged_nvs.hpp"
#include "config/config_codec.hpp"
#include "config/nvs_config_store.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"
#include "../../src/config/nvs_config_store.cpp"

using namespace lamp;
using config_codec::SaveResult;

namespace {
PagedNvs* g_nvs = nullptr;
}  // namespace

esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* out) {
  *out = 1;
  return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t, const char* key, char* out, size_t* len) {
  if (!g_nvs->has(key)) return ESP_ERR_NVS_NOT_FOUND;
  const std::string value = g_nvs->read(key, "");
  if (out == nullptr) {
    *len = value.size() + 1;
    return ESP_OK;
  }
  if (*len < value.size() + 1) return ESP_ERR_NVS_NOT_FOUND;
  std::memcpy(out, value.c_str(), value.size() + 1);
  *len = value.size() + 1;
  return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t, const char* key, const char* value) {
  return g_nvs->write(key, value) > 0 ? ESP_OK : ESP_ERR_NVS_NOT_ENOUGH_SPACE;
}
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* out, size_t* len) {
  static char buf[8192];
  const size_t n = g_nvs->readBlob(key, buf, sizeof(buf));
  if (n == 0) return ESP_ERR_NVS_NOT_FOUND;
  if (out != nullptr) {
    if (*len < n) return ESP_ERR_NVS_NOT_FOUND;
    std::memcpy(out, buf, n);
  }
  *len = n;
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* value, size_t len) {
  switch (g_nvs->rawWriteBlob(key, value, len)) {
    case BlobWrite::Ok: return ESP_OK;
    case BlobWrite::OutOfMemory: return ESP_ERR_NO_MEM;
    default: return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  }
}
esp_err_t nvs_erase_key(nvs_handle_t, const char* key) {
  if (!g_nvs->has(key)) return ESP_ERR_NVS_NOT_FOUND;
  g_nvs->erase(key);
  return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t) { return ESP_OK; }
esp_err_t nvs_find_key(nvs_handle_t, const char* key, nvs_type_t*) {
  return g_nvs->has(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_stats(const char*, nvs_stats_t* stats) {
  *stats = {g_nvs->liveEntries(), 0, 0, 0, 0};
  return ESP_OK;
}
esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_erase() { return ESP_OK; }

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
};

uint8_t s_buf[config_codec::kMaxSettingsBlobLen];

SaveResult save(ConfigStore& store, const Model& m) {
  return config_codec::saveConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode,
                                  s_buf, sizeof(s_buf));
}

Model load(ConfigStore& store) {
  Model m;
  config_codec::loadConfig(store, m.lamp, m.base, m.shade, m.expressions, m.homeMode, s_buf,
                           sizeof(s_buf));
  return m;
}

Model splitModel() {
  Model m;
  m.lamp.name = "device";
  m.base.segments = {{"Base", 4, {Color(1, 1, 1, 1)}}};
  for (const char* type : {"pulse", "shifty", "spotty", "glitchy", "breathing", "flicker"}) {
    ExpressionConfig e;
    e.type = type;
    e.target = 1;
    for (int c = 0; c < 8; ++c) e.colors.push_back(Color(c, 2, 3, 4));
    for (size_t k = 0; k < kMaxExpressionParams; ++k) {
      e.parameters.set(kExpressionParamKeys[k], 0xFFFFFFFFu);
    }
    m.expressions.expressions.push_back(e);
  }
  return m;
}

}  // namespace

// One out-of-memory write of a record, then later saves through the device
// store all land, and the boot after them loads the latest.
void test_the_device_store_writes_past_an_orphan() {
  PagedNvs nvs;
  g_nvs = &nvs;
  NvsConfigStore store;
  Model m = splitModel();
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  nvs.outOfMemoryKey = "x3";
  m.expressions.expressions[3].intervalMin = 1;
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::WriteFailed);
  m.expressions.expressions[3].intervalMin = 2;
  TEST_ASSERT_TRUE(save(store, m) == SaveResult::Ok);
  nvs.boot();
  const Model back = load(store);
  TEST_ASSERT_EQUAL(6, back.expressions.expressions.size());
  TEST_ASSERT_EQUAL_UINT32(2, back.expressions.expressions[3].intervalMin);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_device_store_writes_past_an_orphan);
  return UNITY_END();
}
