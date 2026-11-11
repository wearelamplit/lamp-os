#include "config/nvs_config_store.hpp"

#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>

namespace lamp {

namespace {

constexpr const char* kNamespace = "lamp";
constexpr const char* kWifiNamespace = "nvs.net80211";

void logFailure(const char* op, const char* key, esp_err_t err) {
  Serial.printf("[nvs] %s %s failed: 0x%x\n", op, key, (unsigned)err);
}

// A write that leaves NVS uncommitted is a failed write; the caller retries.
size_t commitOrZero(nvs_handle_t h, esp_err_t err, const char* op,
                    const char* key, size_t len) {
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    logFailure(op, key, err);
    return 0;
  }
  return len;
}

}  // namespace

std::string NvsConfigStore::read(const char* key, const char* defaultValue) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return defaultValue;
  size_t len = 0;
  std::string out;
  if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0) {
    out.resize(len);
    if (nvs_get_str(h, key, out.data(), &len) == ESP_OK) {
      out.resize(len - 1);  // len counts the NUL
      nvs_close(h);
      return out;
    }
  }
  nvs_close(h);
  return defaultValue;
}

size_t NvsConfigStore::readString(const char* key, char* out, size_t cap) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
  size_t len = 0;
  const bool ok = nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0 && len <= cap &&
                  nvs_get_str(h, key, out, &len) == ESP_OK;
  nvs_close(h);
  return ok ? len - 1 : 0;
}

bool NvsConfigStore::has(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  nvs_type_t type;
  const bool found = nvs_find_key(h, key, &type) == ESP_OK;
  nvs_close(h);
  return found;
}

size_t NvsConfigStore::write(const char* key, const char* value) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    logFailure("open", key, err);
    return 0;
  }
  return commitOrZero(h, nvs_set_str(h, key, value), "set_str", key,
                      std::strlen(value) + 1);
}

size_t NvsConfigStore::readBlob(const char* key, void* out, size_t cap) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
  size_t len = 0;
  const bool fits = nvs_get_blob(h, key, nullptr, &len) == ESP_OK && len <= cap;
  const bool ok = fits && nvs_get_blob(h, key, out, &len) == ESP_OK;
  nvs_close(h);
  return ok ? len : 0;
}

size_t NvsConfigStore::writeBlob(const char* key, const void* data, size_t len) {
  return writeBlobPastOrphan(orphans_, key, data, len, rawWriteBlob) ? len : 0;
}

BlobWrite NvsConfigStore::rawWriteBlob(const char* key, const void* data, size_t len) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    logFailure("open", key, err);
    return BlobWrite::Failed;
  }
  err = nvs_set_blob(h, key, data, len);
  if (commitOrZero(h, err, "set_blob", key, 1) > 0) return BlobWrite::Ok;
  return err == ESP_ERR_NO_MEM ? BlobWrite::OutOfMemory : BlobWrite::Failed;
}

bool NvsConfigStore::erase(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_erase_key(h, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool NvsConfigStore::clear() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_erase_all(h);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool NvsConfigStore::initFlash() {
  // initArduino has already run nvs_flash_init (erasing on NO_FREE_PAGES or
  // NEW_VERSION_FOUND); an initialized partition makes this a no-op.
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) {
    // Erase only for the codes that mean the partition itself is unusable;
    // nothing on it is readable through the API then, so nothing is lost
    // that could be saved. Any other error (out of memory, a transient
    // flash fault) leaves it alone and the lamp runs without NVS.
    const bool unusable = err == ESP_ERR_NVS_NO_FREE_PAGES ||
                          err == ESP_ERR_NVS_NEW_VERSION_FOUND ||
                          err == ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    Serial.printf("[nvs] init failed: 0x%x%s\n", (unsigned)err,
                  unusable ? "; erasing NVS (stored settings lost)" : "");
    if (!unusable) return false;
    err = nvs_flash_erase();
    if (err == ESP_OK) err = nvs_flash_init();
    if (err != ESP_OK) {
      Serial.printf("[nvs] re-init after erase failed: 0x%x\n", (unsigned)err);
      return false;
    }
  }
  reclaimWifiNamespace();
  return true;
}

void NvsConfigStore::logUsage() {
  nvs_stats_t stats;
  if (nvs_get_stats(nullptr, &stats) != ESP_OK) return;
  Serial.printf("[nvs] used %u free %u\n", (unsigned)stats.used_entries,
                (unsigned)stats.free_entries);
}

void NvsConfigStore::reclaimWifiNamespace() {
  // The WiFi driver keeps its config in RAM (wifi::begin), so whatever an
  // earlier build stored in its namespace is dead weight in a tight
  // partition. Erasing an empty namespace writes nothing.
  nvs_handle_t h;
  if (nvs_open(kWifiNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_erase_all(h) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

}  // namespace lamp
