#include "config/nvs_config_store.hpp"

namespace lamp {

std::string NvsConfigStore::read(const char* key, const char* defaultValue) {
  // getString returns defaultValue when the namespace can't be opened (fresh
  // chip) or the key is absent, so a failed begin needs no separate guard.
  prefs_.begin("lamp", true);
  String v = prefs_.getString(key, defaultValue);
  prefs_.end();
  return std::string(v.c_str());
}

size_t NvsConfigStore::write(const char* key, const char* value) {
  if (!prefs_.begin("lamp", false)) return 0;
  size_t written = prefs_.putString(key, value);
  prefs_.end();
  return written;
}

bool NvsConfigStore::clear() {
  if (!prefs_.begin("lamp", false)) return false;
  bool cleared = prefs_.clear();
  prefs_.end();
  return cleared;
}

}  // namespace lamp
