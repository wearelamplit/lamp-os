#pragma once
// Native declarations of the ESP-IDF NVS calls NvsConfigStore makes. A test
// that compiles the store defines them (test_nvs_config_store backs them
// with PagedNvs); nothing else includes this.
#include <cstddef>
#include <cstdint>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NO_MEM = 0x101;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
constexpr esp_err_t ESP_ERR_NVS_NOT_ENOUGH_SPACE = 0x1105;
constexpr esp_err_t ESP_ERR_NVS_NO_FREE_PAGES = 0x110d;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND = 0x1110;

using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
enum nvs_type_t { NVS_TYPE_ANY = 0xff };

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_find_key(nvs_handle_t h, const char* key, nvs_type_t* type);
struct nvs_stats_t {
  size_t used_entries;
  size_t free_entries;
  size_t available_entries;
  size_t total_entries;
  size_t namespace_count;
};
esp_err_t nvs_get_stats(const char* part_name, nvs_stats_t* stats);
