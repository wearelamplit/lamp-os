#pragma once

#include <map>
#include <string>

namespace lamp {

// Persistence seam for Config. Owns the key/value backing (NVS on device,
// in-memory in tests), so Config carries no platform dependency and its
// serialization/policy logic can be exercised in the native suite.
//
// Reads return defaultValue when the key is absent or the backing is
// unavailable. write() returns bytes stored (0 = nothing written, e.g. the
// backing couldn't be opened). All access is single-writer on Core 1 (NVS is
// not Core-0-safe); the seam adds no locking of its own.
class ConfigStore {
 public:
  virtual ~ConfigStore() = default;
  virtual std::string read(const char* key, const char* defaultValue) = 0;
  virtual size_t write(const char* key, const char* value) = 0;
  // Wipe the whole namespace (factory reset). Returns true on success.
  virtual bool clear() = 0;
};

// In-memory ConfigStore for the native suite (no Arduino/flash). The second
// implementation that makes ConfigStore a test seam rather than speculative
// indirection (see docs/dev/code-smells.md).
class InMemoryConfigStore : public ConfigStore {
 public:
  std::string read(const char* key, const char* defaultValue) override {
    auto it = map_.find(key);
    return it == map_.end() ? std::string(defaultValue) : it->second;
  }
  size_t write(const char* key, const char* value) override {
    map_[key] = value;
    return map_[key].size();
  }
  bool clear() override {
    map_.clear();
    return true;
  }

 private:
  std::map<std::string, std::string> map_;
};

}  // namespace lamp
