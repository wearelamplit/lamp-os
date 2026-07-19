// Minimal Preferences stub for native unit tests.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// ponytail: flat array KV store, capped at 32 entries — covers test volume.
namespace {
struct KvEntry { char key[16]; char val[32]; bool used = false; };
constexpr size_t kKvCap = 32;
KvEntry s_kv[kKvCap];

static void kv_reset() { for (auto& e : s_kv) e.used = false; }

static KvEntry* kv_find(const char* k) {
  for (auto& e : s_kv) if (e.used && strcmp(e.key, k) == 0) return &e;
  return nullptr;
}
static KvEntry* kv_slot(const char* k) {
  KvEntry* e = kv_find(k);
  if (e) return e;
  for (auto& x : s_kv) if (!x.used) {
    x.used = true; strncpy(x.key, k, 15); x.key[15] = '\0'; return &x;
  }
  return nullptr;
}
}  // namespace

class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  void end() {}
  bool putString(const char* k, const std::string& v) {
    auto* e = kv_slot(k); if (!e) return false;
    strncpy(e->val, v.c_str(), 31); e->val[31] = '\0'; return true;
  }
  std::string getString(const char* k, const std::string& def = "") {
    const auto* e = kv_find(k); return e ? std::string(e->val) : def;
  }
  bool putInt(const char* k, int v) {
    auto* e = kv_slot(k); if (!e) return false;
    snprintf(e->val, sizeof(e->val), "%d", v); return true;
  }
  int getInt(const char* k, int def = 0) {
    const auto* e = kv_find(k); return e ? atoi(e->val) : def;
  }
  bool putUInt(const char* k, uint32_t v) {
    auto* e = kv_slot(k); if (!e) return false;
    snprintf(e->val, sizeof(e->val), "%u", (unsigned)v); return true;
  }
  uint32_t getUInt(const char* k, uint32_t def = 0) {
    const auto* e = kv_find(k); return e ? (uint32_t)strtoul(e->val, nullptr, 10) : def;
  }
  bool putUChar(const char* k, uint8_t v) {
    auto* e = kv_slot(k); if (!e) return false;
    snprintf(e->val, sizeof(e->val), "%u", (unsigned)v); return true;
  }
  uint8_t getUChar(const char* k, uint8_t def = 0) {
    const auto* e = kv_find(k); return e ? (uint8_t)atoi(e->val) : def;
  }
  bool putUShort(const char* k, uint16_t v) {
    auto* e = kv_slot(k); if (!e) return false;
    snprintf(e->val, sizeof(e->val), "%u", (unsigned)v); return true;
  }
  uint16_t getUShort(const char* k, uint16_t def = 0) {
    const auto* e = kv_find(k); return e ? (uint16_t)strtoul(e->val, nullptr, 10) : def;
  }
  size_t getBytesLength(const char*) { return 0; }
  size_t getBytes(const char*, void*, size_t) { return 0; }
  size_t putBytes(const char*, const void*, size_t) { return 0; }
  bool   remove(const char* k) { auto* e = kv_find(k); if (e) e->used = false; return true; }
};
