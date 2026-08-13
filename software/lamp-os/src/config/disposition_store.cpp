#include "config/disposition_store.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>

#include "util/bd_addr.hpp"

namespace lamp {

namespace {
using MacKey = std::array<uint8_t, 6>;
using Entry = std::pair<MacKey, uint8_t>;

inline MacKey keyOf(const uint8_t mac[6]) {
  MacKey k;
  std::copy(mac, mac + 6, k.begin());
  return k;
}

// Comparator for std::lower_bound over the sorted dispositions vector.
// Compares an existing entry's key against the target lookup key.
inline bool entryLess(const Entry& a, const MacKey& b) { return a.first < b; }

// Parse a JSON object of colon-hex mac -> disposition into a fresh key-sorted,
// deduped, clamped vector. Shared by load() and setFromJson(). parseBdAddr both
// validates and unpacks the key; legacy name-keyed entries (older firmware)
// fail it and are silently dropped, so the next write rewrites the NVS blob.
bool parseEntries(const char* json, size_t len, std::vector<Entry>& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return false;
  }
  if (!doc.is<JsonObject>()) return false;
  out.clear();
  out.reserve(DispositionStore::kMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (out.size() >= DispositionStore::kMax) break;
    MacKey mac;
    if (!parseBdAddr(kv.key().c_str(), mac.data())) continue;
    uint32_t v = kv.value() | (uint32_t)DispositionStore::kDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    out.emplace_back(mac, static_cast<uint8_t>(v));
  }
  // Sort once (O(N log N) vs the O(N^2) of an N-call set() loop, each
  // shifting the tail). Byte order == canonical colon-hex string order.
  std::sort(out.begin(), out.end(),
            [](const Entry& a, const Entry& b) { return a.first < b.first; });
  // Dedupe: ArduinoJson is permissive on malformed input and may emit
  // duplicate keys. Last write wins.
  auto last = std::unique(
      out.begin(), out.end(),
      [](const Entry& a, const Entry& b) { return a.first == b.first; });
  out.erase(last, out.end());
  return true;
}
}  // namespace

DispositionStore::DispositionStore(uint32_t flushIdleMs)
    : debouncer_(flushIdleMs) {
  mutex_ = xSemaphoreCreateMutex();
}

void DispositionStore::load() {
  if (!store_) return;
  std::string json = store_->read("dispositions", "{}");

  std::vector<Entry> next;
  if (!parseEntries(json.c_str(), json.size(), next)) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  entries_ = std::move(next);
  xSemaphoreGive(mutex_);
}

uint8_t DispositionStore::get(const uint8_t mac[6]) const {
  const MacKey key = keyOf(mac);
  // Binary search on the sorted vector. lower_bound returns the first entry
  // >= key; still compare keys because it can land on a strictly-greater
  // neighbour.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key, entryLess);
  uint8_t value = (it == entries_.end() || it->first != key) ? kDefault
                                                             : it->second;
  xSemaphoreGive(mutex_);
  return value;
}

void DispositionStore::set(const uint8_t mac[6], uint8_t value,
                           uint32_t nowMs) {
  const MacKey key = keyOf(mac);
  if (value < 1) value = 1;
  if (value > 5) value = 5;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key, entryLess);
  if (it != entries_.end() && it->first == key) {
    // Update in place. No resize, no shift; sort order preserved.
    it->second = value;
  } else {
    if (entries_.size() >= kMax) {
      // Evict the lowest-by-key entry. Best-effort at the cap; users typically
      // have <100 paired lamps.
      entries_.erase(entries_.begin());
      it = std::lower_bound(entries_.begin(), entries_.end(), key, entryLess);
    }
    entries_.insert(it, std::make_pair(key, value));
  }
  xSemaphoreGive(mutex_);
  // Defer the write (see DispositionDebouncer). The Core 1 drain flushes
  // once the slider goes idle; BLE disconnect force-flushes.
  debouncer_.markDirty(nowMs);
}

std::string DispositionStore::asJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<Entry> snapshot = entries_;
  xSemaphoreGive(mutex_);
  // Sorted-vector iteration yields keys in ascending byte (== colon-hex)
  // order, a stable on-disk shape. Format each MAC back to canonical
  // colon-hex only here, at serialize time. ArduinoJson copies the char[]
  // key, so the reused buffer is safe.
  JsonDocument doc;
  char keyBuf[18];
  for (const auto& kv : snapshot) {
    formatBdAddr(kv.first.data(), keyBuf);
    doc[keyBuf] = kv.second;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool DispositionStore::setFromJson(const char* json, size_t len,
                                   uint32_t nowMs) {
  std::vector<Entry> next;
  if (!parseEntries(json, len, next)) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  entries_ = std::move(next);
  xSemaphoreGive(mutex_);
  debouncer_.markDirty(nowMs);
  return true;
}

bool DispositionStore::persist_() {
  if (!store_) return false;
  std::string out = asJson();
  // A 0-byte write means NVS is full or corrupt; leave the dirty flag set so
  // the next flush retries.
  return store_->write("dispositions", out.c_str()) > 0;
}

void DispositionStore::maybeFlush(uint32_t nowMs) {
  if (!debouncer_.shouldFlush(nowMs)) return;
  if (persist_()) {
    debouncer_.clear();
  }
}

void DispositionStore::flushNow() {
  if (!debouncer_.dirty()) return;
  if (persist_()) {
    debouncer_.clear();
  }
}

}  // namespace lamp
