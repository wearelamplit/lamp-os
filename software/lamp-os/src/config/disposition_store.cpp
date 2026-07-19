#include "config/disposition_store.hpp"

#include <ArduinoJson.h>

#include <algorithm>

#include "util/bd_addr.hpp"

namespace lamp {

namespace {
// Comparator for std::lower_bound over the sorted dispositions vector.
// Compares an existing entry's key against the target lookup key.
inline bool entryLess(const std::pair<std::string, uint8_t>& a,
                      const std::string& b) {
  return a.first < b;
}

// Parse a JSON object of lampId -> disposition into a fresh key-sorted,
// deduped, clamped vector. Shared by load() and setFromJson().
bool parseEntries(const char* json, size_t len,
                  std::vector<std::pair<std::string, uint8_t>>& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return false;
  }
  if (!doc.is<JsonObject>()) return false;
  out.clear();
  out.reserve(DispositionStore::kMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (out.size() >= DispositionStore::kMax) break;
    const char* key = kv.key().c_str();
    // Dispositions are keyed by lampId (mac). Legacy name-keyed entries
    // (older firmware) fail this filter and are silently dropped; the next
    // write overwrites the NVS blob in the new shape.
    if (!isValidBdAddr(key)) continue;
    uint32_t v = kv.value() | (uint32_t)DispositionStore::kDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    out.emplace_back(std::string(key), static_cast<uint8_t>(v));
  }
  // Sort once (O(N log N) vs the O(N^2) of an N-call set() loop, each
  // shifting the tail).
  std::sort(out.begin(), out.end(),
            [](const std::pair<std::string, uint8_t>& a,
               const std::pair<std::string, uint8_t>& b) {
              return a.first < b.first;
            });
  // Dedupe: ArduinoJson is permissive on malformed input and may emit
  // duplicate keys. Last write wins.
  auto last = std::unique(
      out.begin(), out.end(),
      [](const std::pair<std::string, uint8_t>& a,
         const std::pair<std::string, uint8_t>& b) { return a.first == b.first; });
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

  std::vector<std::pair<std::string, uint8_t>> next;
  if (!parseEntries(json.c_str(), json.size(), next)) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  entries_ = std::move(next);
  xSemaphoreGive(mutex_);
}

uint8_t DispositionStore::get(const std::string& lampId) const {
  // Binary search on the sorted vector. lower_bound returns the first entry
  // >= lampId; still compare keys because it can land on a strictly-greater
  // neighbour.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), lampId, entryLess);
  uint8_t value = (it == entries_.end() || it->first != lampId) ? kDefault
                                                                : it->second;
  xSemaphoreGive(mutex_);
  return value;
}

void DispositionStore::set(const std::string& lampId, uint8_t value,
                           uint32_t nowMs) {
  if (lampId.empty()) return;
  if (value < 1) value = 1;
  if (value > 5) value = 5;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), lampId, entryLess);
  if (it != entries_.end() && it->first == lampId) {
    // Update in place. No resize, no shift; sort order preserved.
    it->second = value;
  } else {
    if (entries_.size() >= kMax) {
      // Evict the lowest-by-key entry. Best-effort at the cap; users typically
      // have <100 paired lamps.
      entries_.erase(entries_.begin());
      it = std::lower_bound(entries_.begin(), entries_.end(), lampId, entryLess);
    }
    entries_.insert(it, std::make_pair(lampId, value));
  }
  xSemaphoreGive(mutex_);
  // Defer the write (see DispositionDebouncer). The Core 1 drain flushes
  // once the slider goes idle; BLE disconnect force-flushes.
  debouncer_.markDirty(nowMs);
}

std::string DispositionStore::asJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<std::pair<std::string, uint8_t>> snapshot = entries_;
  xSemaphoreGive(mutex_);
  // Sorted-vector iteration yields keys in lexicographic order, a stable
  // on-disk byte shape across reads.
  JsonDocument doc;
  for (const auto& kv : snapshot) {
    doc[kv.first.c_str()] = kv.second;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool DispositionStore::setFromJson(const char* json, size_t len,
                                   uint32_t nowMs) {
  std::vector<std::pair<std::string, uint8_t>> next;
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
