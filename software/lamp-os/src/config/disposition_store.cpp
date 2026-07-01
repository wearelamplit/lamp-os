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
}  // namespace

void DispositionStore::load() {
  if (!store_) return;
  std::string json = store_->read("dispositions", "{}");

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;

  // Two-pass load: gather into a fresh vector, then sort once (O(N log N) vs
  // the O(N^2) of an N-call set() loop, each shifting the tail). Reserve to
  // skip reallocation for typical loads.
  entries_.clear();
  entries_.reserve(kMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (entries_.size() >= kMax) break;
    const char* key = kv.key().c_str();
    // Dispositions are keyed by BD_ADDR. Legacy name-keyed entries (older
    // firmware) fail this filter and are silently dropped; the next write
    // overwrites the NVS blob in the new shape.
    if (!isValidBdAddr(key)) continue;
    uint32_t v = kv.value() | (uint32_t)kDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    entries_.emplace_back(std::string(key), static_cast<uint8_t>(v));
  }
  std::sort(entries_.begin(), entries_.end(),
            [](const std::pair<std::string, uint8_t>& a,
               const std::pair<std::string, uint8_t>& b) {
              return a.first < b.first;
            });
  // Dedupe defensively. JSON objects shouldn't have duplicate keys, but
  // ArduinoJson is permissive on bad input. Last write wins.
  auto last = std::unique(
      entries_.begin(), entries_.end(),
      [](const std::pair<std::string, uint8_t>& a,
         const std::pair<std::string, uint8_t>& b) { return a.first == b.first; });
  entries_.erase(last, entries_.end());
}

uint8_t DispositionStore::get(const std::string& bdAddr) const {
  // Binary search on the sorted vector. lower_bound returns the first entry
  // >= bdAddr; still compare keys because it can land on a strictly-greater
  // neighbour.
  auto it = std::lower_bound(entries_.begin(), entries_.end(), bdAddr, entryLess);
  if (it == entries_.end() || it->first != bdAddr) {
    return kDefault;
  }
  return it->second;
}

void DispositionStore::set(const std::string& bdAddr, uint8_t value,
                           uint32_t nowMs) {
  if (bdAddr.empty()) return;
  if (value < 1) value = 1;
  if (value > 5) value = 5;
  auto it = std::lower_bound(entries_.begin(), entries_.end(), bdAddr, entryLess);
  if (it != entries_.end() && it->first == bdAddr) {
    // Update in place. No resize, no shift; sort order preserved.
    it->second = value;
  } else {
    if (entries_.size() >= kMax) {
      // Evict the lowest-by-key entry (matches the historical std::map
      // iteration-order eviction). Best-effort at the cap; users typically
      // have <100 paired lamps.
      entries_.erase(entries_.begin());
      it = std::lower_bound(entries_.begin(), entries_.end(), bdAddr, entryLess);
    }
    entries_.insert(it, std::make_pair(bdAddr, value));
  }
  // Defer the write (see DispositionDebouncer). The Core 1 drain flushes
  // once the slider goes idle; BLE disconnect force-flushes.
  debouncer_.markDirty(nowMs);
}

std::string DispositionStore::asJson() const {
  // Sorted-vector iteration yields keys in lexicographic order, a stable
  // on-disk byte shape across reads.
  JsonDocument doc;
  for (const auto& kv : entries_) {
    doc[kv.first.c_str()] = kv.second;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool DispositionStore::setFromJson(const char* json, size_t len,
                                   uint32_t nowMs) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return false;
  }
  if (!doc.is<JsonObject>()) return false;
  // Bulk replace: stage into a fresh vector, sort once (same O(N log N)
  // strategy as load()).
  std::vector<std::pair<std::string, uint8_t>> next;
  next.reserve(kMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (next.size() >= kMax) break;
    const char* key = kv.key().c_str();
    if (!isValidBdAddr(key)) continue;
    uint32_t v = kv.value() | (uint32_t)kDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    next.emplace_back(std::string(key), static_cast<uint8_t>(v));
  }
  std::sort(next.begin(), next.end(),
            [](const std::pair<std::string, uint8_t>& a,
               const std::pair<std::string, uint8_t>& b) {
              return a.first < b.first;
            });
  auto last = std::unique(
      next.begin(), next.end(),
      [](const std::pair<std::string, uint8_t>& a,
         const std::pair<std::string, uint8_t>& b) { return a.first == b.first; });
  next.erase(last, next.end());
  entries_ = std::move(next);
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
