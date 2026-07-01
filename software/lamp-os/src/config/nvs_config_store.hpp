#pragma once

#include <Preferences.h>

#include <string>

#include "config_store.hpp"

namespace lamp {

// NVS-backed ConfigStore: all keys live in the "lamp" Preferences namespace.
// begin/end is paired per call; a failed begin (NVS full or partition corrupt)
// degrades to default-on-read / 0-on-write rather than faulting. Must be
// driven from Core 1 (NVS is not Core-0-safe).
class NvsConfigStore : public ConfigStore {
 public:
  std::string read(const char* key, const char* defaultValue) override;
  size_t write(const char* key, const char* value) override;
  bool clear() override;

 private:
  Preferences prefs_;
};

}  // namespace lamp
