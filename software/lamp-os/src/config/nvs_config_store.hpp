#pragma once

#include <string>

#include "config_store.hpp"
#include "orphan_guard.hpp"

namespace lamp {

// NVS-backed ConfigStore: all keys live in the "lamp" namespace. Talks to the
// nvs_* API directly: Preferences logs its own failures through a path that
// faults on a fragmented heap, and a full NVS must come back as a returned 0.
// Each failure logs one line. Blob writes go through writeBlobPastOrphan, so
// a key whose write ran out of heap is rewritten off the orphan it left. Must
// be driven from the loop task.
class NvsConfigStore : public ConfigStore {
 public:
  std::string read(const char* key, const char* defaultValue) override;
  size_t readString(const char* key, char* out, size_t cap) override;
  size_t write(const char* key, const char* value) override;
  size_t readBlob(const char* key, void* out, size_t cap) override;
  size_t writeBlob(const char* key, const void* data, size_t len) override;
  bool has(const char* key) override;
  bool erase(const char* key) override;
  bool clear() override;

  // Boot gate, before the lamp opens NVS: makes sure the partition is
  // initialized, erasing and re-initializing it only when it reports itself
  // unusable (every stored key is lost then, and the lamp still boots with
  // BLE), then reclaims the WiFi driver's stored config. Returns true when NVS is usable.
  static bool initFlash();
  // Logs the partition's used and free entries, every namespace's.
  static void logUsage();

 private:
  static void reclaimWifiNamespace();
  static BlobWrite rawWriteBlob(const char* key, const void* data, size_t len);
  OrphanKeys orphans_;
};

}  // namespace lamp
