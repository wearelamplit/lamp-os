#include "config/disposition_store.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "util/bd_addr.hpp"
#include "util/heap_guard.hpp"

namespace lamp {

namespace {
using MacKey = std::array<uint8_t, 6>;

inline MacKey keyOf(const uint8_t mac[6]) {
  MacKey k;
  std::copy(mac, mac + 6, k.begin());
  return k;
}

inline uint8_t clampDisp(uint32_t v) {
  return static_cast<uint8_t>(std::clamp<uint32_t>(v, 1, 5));
}

// Rows in a blob of `len` bytes of the given format, or -1 when malformed.
int blobRows(const uint8_t* data, size_t len) {
  if (len == 0) return -1;
  size_t header = 0;
  size_t row = 0;
  if (data[0] == DispositionStore::kBlobFormat) {
    header = DispositionStore::kBlobHeaderLen;
    row = DispositionStore::kBlobRowLen;
  } else if (data[0] == DispositionStore::kLegacyBlobFormat) {
    header = 1;
    row = DispositionStore::kLegacyBlobRowLen;
  } else {
    return -1;
  }
  if (len < header || (len - header) % row != 0) return -1;
  const size_t count = (len - header) / row;
  return count > DispositionStore::kMax ? -1 : static_cast<int>(count);
}

size_t validJsonEntries(JsonObject obj) {
  size_t n = 0;
  uint8_t mac[6];
  for (JsonPair kv : obj) {
    if (parseBdAddr(kv.key().c_str(), mac)) ++n;
  }
  return n;
}
}  // namespace

DispositionStore::DispositionStore(uint32_t flushIdleMs) : debouncer_(flushIdleMs) {
  entries_.reserve(kMax);
  mutex_ = xSemaphoreCreateMutex();
}

uint32_t DispositionStore::nowMin(uint64_t nowMs) {
  return UINT16_MAX + static_cast<uint32_t>(nowMs / 60000);
}

uint16_t DispositionStore::unseenMin(const Row& row, uint32_t now) {
  if (now <= row.seenAtMin) return 0;
  return static_cast<uint16_t>(std::min<uint32_t>(now - row.seenAtMin, UINT16_MAX));
}

uint16_t DispositionStore::strength(const Row& row) {
  return static_cast<uint16_t>(std::abs(row.disp - kDefault) * 128 + std::abs(row.score) +
                               std::min(row.meets, kRegularMeetsCap) * kRegularWeight);
}

DispositionStore::Row DispositionStore::freshRow(const uint8_t mac[6], uint32_t now) {
  return Row{keyOf(mac), kDefault, 0, 0, UINT8_MAX, now};
}

DispositionStore::Row* DispositionStore::findLocked_(const MacKey& key) {
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                             [](const Row& r, const MacKey& k) { return r.mac < k; });
  return (it != entries_.end() && it->mac == key) ? &*it : nullptr;
}

DispositionStore::Row* DispositionStore::admitLocked_(const Row& fresh, bool force,
                                                      uint32_t now) {
  if (entries_.size() >= kMax) {
    auto weakest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      const uint16_t s = strength(*it);
      const uint16_t w = strength(*weakest);
      if (s < w || (s == w && unseenMin(*it, now) > unseenMin(*weakest, now))) weakest = it;
    }
    const uint16_t f = strength(fresh);
    const uint16_t w = strength(*weakest);
    const bool beats = f > w || (f == w && unseenMin(*weakest, now) >= kAbsentMin);
    if (!force && !beats) return nullptr;
    entries_.erase(weakest);
  }
  auto it = std::lower_bound(entries_.begin(), entries_.end(), fresh.mac,
                             [](const Row& r, const MacKey& k) { return r.mac < k; });
  return &*entries_.insert(it, fresh);
}

bool DispositionStore::userSetLocked_(const uint8_t mac[6], uint8_t value, uint32_t now) {
  Row* row = findLocked_(keyOf(mac));
  if (row) {
    if (row->disp == value) return false;
    row->disp = value;
    row->score = 0;
    return true;
  }
  if (value == kDefault) return false;
  Row fresh = freshRow(mac, now);
  fresh.disp = value;
  admitLocked_(fresh, true, now);
  return true;
}

void DispositionStore::load(uint64_t nowMs) {
  if (!store_) return;
  const uint32_t now = nowMin(nowMs);
  std::array<uint8_t, kMaxBlobLen> blob;
  const size_t n = store_->readBlob(kBlobKey, blob.data(), blob.size());
  const int blobCount = n > 0 ? blobRows(blob.data(), n) : -1;
  const bool blobOk = blobCount >= 0;
  // The legacy JSON key exists only before the first migration, or after an
  // older firmware rewrote it following a downgrade; either way it is the
  // newest copy, so it wins over a stale blob. A JSON that does not parse, or
  // holds nothing while the blob holds rows, never replaces the blob.
  const std::string json = store_->read(kLegacyJsonKey, "");
  JsonDocument doc;
  bool fromJson = false;
  if (!json.empty() && deserializeJson(doc, json.c_str(), json.size()) == DeserializationError::Ok &&
      doc.is<JsonObject>()) {
    fromJson = !(validJsonEntries(doc.as<JsonObject>()) == 0 && blobOk && blobCount > 0);
  }
  if (!fromJson && !json.empty()) store_->erase(kLegacyJsonKey);
  if (!fromJson && !blobOk) return;

  size_t pruned = 0;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  entries_.clear();
  flags_ = kFlagEngineOn;
  if (fromJson) {
    // Document order, so a repeated key's last value wins.
    for (JsonPair kv : doc.as<JsonObject>()) {
      uint8_t mac[6];
      if (!parseBdAddr(kv.key().c_str(), mac)) continue;
      userSetLocked_(mac, clampDisp(kv.value() | (uint32_t)kDefault), now);
    }
  } else {
    const bool v2 = blob[0] == kBlobFormat;
    if (v2) flags_ = blob[1];
    const size_t header = v2 ? kBlobHeaderLen : 1;
    const size_t rowLen = v2 ? kBlobRowLen : kLegacyBlobRowLen;
    for (int i = 0; i < blobCount; ++i) {
      const uint8_t* e = blob.data() + header + i * rowLen;
      Row row = freshRow(e, now);
      row.disp = clampDisp(e[6]);
      if (v2) {
        row.score = static_cast<int8_t>(std::max<int>(static_cast<int8_t>(e[7]), -127));
        row.meets = e[8];
        row.meetAgeH = e[9];
        row.seenAtMin = now - static_cast<uint16_t>(e[10] | (e[11] << 8));
      }
      entries_.push_back(row);
    }
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Row& a, const Row& b) { return a.mac < b.mac; });
    entries_.erase(std::unique(entries_.begin(), entries_.end(),
                               [](const Row& a, const Row& b) { return a.mac == b.mac; }),
                   entries_.end());
    const size_t before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Row& r) {
                                    return r.disp == kDefault && unseenMin(r, now) >= kPruneMin;
                                  }),
                   entries_.end());
    pruned = before - entries_.size();
  }
  const size_t rows = entries_.size();
  xSemaphoreGive(mutex_);
  Serial.printf("[disp] %u peers from %s, %u pruned\n", static_cast<unsigned>(rows),
                fromJson ? "json" : (blob[0] == kBlobFormat ? "format 2" : "format 1"),
                static_cast<unsigned>(pruned));
  // The JSON is about three times the blob, and a lamp whose NVS is full gets
  // that space back on this boot. The blob lands and reads back before the
  // JSON goes, so a reset between them keeps a readable copy.
  if (fromJson && !persist_(nowMs)) debouncer_.markDirty(static_cast<uint32_t>(nowMs));
}

PeerRecord DispositionStore::peer(const uint8_t mac[6], uint64_t nowMs) const {
  const MacKey key = keyOf(mac);
  PeerRecord out;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                             [](const Row& r, const MacKey& k) { return r.mac < k; });
  if (it != entries_.end() && it->mac == key) {
    out.disp = it->disp;
    out.score = it->score;
    out.meets = it->meets;
    out.meetAgeH = it->meetAgeH;
    out.unseenMin = unseenMin(*it, nowMin(nowMs));
  }
  xSemaphoreGive(mutex_);
  return out;
}

void DispositionStore::set(const uint8_t mac[6], uint8_t value, uint64_t nowMs) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool changed = userSetLocked_(mac, clampDisp(value), nowMin(nowMs));
  xSemaphoreGive(mutex_);
  if (changed) debouncer_.markDirty(static_cast<uint32_t>(nowMs));
}

bool DispositionStore::setFromJson(const char* json, size_t len, uint64_t nowMs) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  if (!doc.is<JsonObject>()) return false;
  const uint32_t now = nowMin(nowMs);
  bool changed = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (JsonPair kv : doc.as<JsonObject>()) {
    uint8_t mac[6];
    if (!parseBdAddr(kv.key().c_str(), mac)) continue;
    changed = userSetLocked_(mac, clampDisp(kv.value() | (uint32_t)kDefault), now) || changed;
  }
  xSemaphoreGive(mutex_);
  if (changed) debouncer_.markDirty(static_cast<uint32_t>(nowMs));
  return true;
}

NudgeResult DispositionStore::nudge(const uint8_t mac[6], int16_t delta,
                                    int16_t familiarityDelta, uint64_t nowMs) {
  const uint32_t now = nowMin(nowMs);
  NudgeResult result;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  Row* row = findLocked_(keyOf(mac));
  Row next = row ? *row : freshRow(mac, now);
  result.from = result.to = next.disp;
  const int familiarity = (familiarityDelta > 0 && next.disp >= 4) ? 0 : familiarityDelta;
  const int step = delta + familiarity;
  if (!(flags_ & kFlagEngineOn) || step == 0) {
    xSemaphoreGive(mutex_);
    return result;
  }
  int score = next.score + step;
  if (score >= 127) {
    if (next.disp < 5) {
      ++next.disp;
      score = 0;
    } else {
      score = 127;
    }
  } else if (score <= -127) {
    if (next.disp > 1) {
      --next.disp;
      score = 0;
    } else {
      score = -127;
    }
  }
  next.score = static_cast<int8_t>(score);
  const bool changed = !row || next.score != row->score || next.disp != row->disp;
  if (row) {
    *row = next;
  } else if (!admitLocked_(next, false, now)) {
    xSemaphoreGive(mutex_);
    return result;
  }
  xSemaphoreGive(mutex_);
  if (changed) engineDirty_ = true;
  result.to = next.disp;
  result.stepped = result.to != result.from;
  return result;
}

bool DispositionStore::meet(const uint8_t mac[6], uint8_t meetEveryH, uint64_t nowMs) {
  const uint32_t now = nowMin(nowMs);
  bool counted = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (flags_ & kFlagEngineOn) {
    Row* row = findLocked_(keyOf(mac));
    if (row) {
      if (row->meetAgeH >= meetEveryH) {
        if (row->meets < UINT8_MAX) ++row->meets;
        row->meetAgeH = 0;
        counted = true;
      }
    } else {
      Row fresh = freshRow(mac, now);
      fresh.meets = 1;
      fresh.meetAgeH = 0;
      counted = admitLocked_(fresh, false, now) != nullptr;
    }
  }
  xSemaphoreGive(mutex_);
  if (counted) engineDirty_ = true;
  return counted;
}

void DispositionStore::markSeen(const uint8_t mac[6], uint64_t nowMs) {
  const uint32_t now = nowMin(nowMs);
  bool changed = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  Row* row = (flags_ & kFlagEngineOn) ? findLocked_(keyOf(mac)) : nullptr;
  if (row && unseenMin(*row, now) > 0) {
    row->seenAtMin = now;
    changed = true;
  }
  xSemaphoreGive(mutex_);
  if (changed) ageDirty_ = true;
}

void DispositionStore::decayTowardZero(uint8_t step, uint64_t nowMs) {
  const uint32_t now = nowMin(nowMs);
  bool changed = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (flags_ & kFlagEngineOn) {
    for (Row& r : entries_) {
      if (r.score == 0 || unseenMin(r, now) < kAbsentMin) continue;
      const int s = r.score;
      r.score = static_cast<int8_t>(s > 0 ? std::max(0, s - step) : std::min(0, s + step));
      changed = true;
    }
  }
  xSemaphoreGive(mutex_);
  if (changed) engineDirty_ = true;
}

void DispositionStore::tickHour(uint64_t nowMs) {
  const uint32_t now = nowMin(nowMs);
  bool aging = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (flags_ & kFlagEngineOn) {
    for (Row& r : entries_) {
      if (r.meetAgeH < UINT8_MAX) ++r.meetAgeH;
      // A stored age still short of saturation goes stale until the next flush.
      aging = aging || r.meetAgeH < UINT8_MAX || unseenMin(r, now) < UINT16_MAX;
    }
  }
  xSemaphoreGive(mutex_);
  if (aging) ageDirty_ = true;
}

bool DispositionStore::engineEnabled() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool on = flags_ & kFlagEngineOn;
  xSemaphoreGive(mutex_);
  return on;
}

void DispositionStore::setEngineEnabled(bool on, uint64_t nowMs) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint8_t next = on ? (flags_ | kFlagEngineOn) : (flags_ & ~kFlagEngineOn);
  const bool changed = next != flags_;
  flags_ = next;
  xSemaphoreGive(mutex_);
  if (changed) debouncer_.markDirty(static_cast<uint32_t>(nowMs));
}

size_t DispositionStore::jsonLen(size_t entries) {
  return 2 + entries * (kBdAddrLen + 4) + (entries ? entries - 1 : 0);
}

void DispositionStore::writeJsonLocked_(char* out) const {
  // The compact shape ArduinoJson emits for the same object, written
  // directly. Sorted-vector order is ascending byte order, which is
  // canonical colon-hex order.
  char* p = out;
  *p++ = '{';
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (i) *p++ = ',';
    *p++ = '"';
    formatBdAddr(entries_[i].mac.data(), p);
    p += kBdAddrLen;
    *p++ = '"';
    *p++ = ':';
    *p++ = static_cast<char>('0' + entries_[i].disp);
  }
  *p++ = '}';
  *p = '\0';
}

std::string DispositionStore::asJson() const {
  // Sized from the entry count, allocated outside the lock so a failed
  // allocation never leaves it held, and only when the heap can spare it
  // (this runs on the NimBLE host task); written once the count is seen
  // unchanged under the lock.
  std::string out;
  for (int attempt = 0; attempt < 2; ++attempt) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const size_t len = jsonLen(entries_.size());
    xSemaphoreGive(mutex_);
    if (!heapCanSpare(len + 1)) return std::string();
    try {
      out.resize(len);
    } catch (const std::bad_alloc&) {
      return std::string();
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool unchanged = jsonLen(entries_.size()) == len;
    if (unchanged) writeJsonLocked_(out.data());
    xSemaphoreGive(mutex_);
    if (unchanged) return out;
  }
  return std::string();
}

size_t DispositionStore::encodeLocked_(uint8_t* out, uint32_t now) const {
  out[0] = kBlobFormat;
  out[1] = flags_;
  size_t len = kBlobHeaderLen;
  for (const Row& r : entries_) {
    uint8_t* e = out + len;
    std::copy(r.mac.begin(), r.mac.end(), e);
    e[6] = r.disp;
    e[7] = static_cast<uint8_t>(r.score);
    e[8] = r.meets;
    e[9] = r.meetAgeH;
    const uint16_t unseen = unseenMin(r, now);
    e[10] = static_cast<uint8_t>(unseen);
    e[11] = static_cast<uint8_t>(unseen >> 8);
    len += kBlobRowLen;
  }
  return len;
}

bool DispositionStore::matchesLocked_(const uint8_t* blob, size_t len, uint32_t now) const {
  if (len != kBlobHeaderLen + entries_.size() * kBlobRowLen) return false;
  if (blob[0] != kBlobFormat || blob[1] != flags_) return false;
  for (size_t i = 0; i < entries_.size(); ++i) {
    const Row& r = entries_[i];
    const uint8_t* e = blob + kBlobHeaderLen + i * kBlobRowLen;
    const uint16_t unseen = unseenMin(r, now);
    if (!std::equal(r.mac.begin(), r.mac.end(), e) || e[6] != r.disp ||
        e[7] != static_cast<uint8_t>(r.score) || e[8] != r.meets || e[9] != r.meetAgeH ||
        e[10] != static_cast<uint8_t>(unseen) || e[11] != static_cast<uint8_t>(unseen >> 8)) {
      return false;
    }
  }
  return true;
}

bool DispositionStore::persist_(uint64_t nowMs) {
  if (!store_) return false;
  const uint32_t now = nowMin(nowMs);
  // At most kMaxBlobLen bytes, so the stack holds it and a flush touches no
  // heap. Rows change only on Core 1, the flush's own task, so the read-back
  // compares against the rows as written.
  std::array<uint8_t, kMaxBlobLen> blob;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t len = encodeLocked_(blob.data(), now);
  xSemaphoreGive(mutex_);
  if (store_->writeBlob(kBlobKey, blob.data(), len) == 0) return false;
  blob.fill(0);
  if (store_->readBlob(kBlobKey, blob.data(), blob.size()) != len) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool verified = matchesLocked_(blob.data(), len, now);
  xSemaphoreGive(mutex_);
  if (!verified) return false;
  store_->erase(kLegacyJsonKey);
  return true;
}

void DispositionStore::flushIfDue_(uint64_t nowMs) {
  if (!retry_.due(static_cast<uint32_t>(nowMs))) return;
  if (persist_(nowMs)) {
    debouncer_.clear();
    engineDirty_ = false;
    ageDirty_ = false;
    lastFlushMs_ = nowMs;
    retry_.succeed();
  } else {
    retry_.fail(static_cast<uint32_t>(nowMs));
    Serial.printf("[nvs] dispositions flush failed; retry in %lus\n",
                  (unsigned long)(retry_.waitMs() / 1000));
  }
}

void DispositionStore::maybeFlush(uint64_t nowMs) {
  const uint64_t since = nowMs >= lastFlushMs_ ? nowMs - lastFlushMs_ : 0;
  if (debouncer_.shouldFlush(static_cast<uint32_t>(nowMs)) ||
      (engineDirty_ && since >= kEngineFlushFloorMs) ||
      (ageDirty_ && since >= kAgeFlushFloorMs)) {
    flushIfDue_(nowMs);
  }
}

void DispositionStore::flushNow(uint64_t nowMs) {
  if (debouncer_.dirty()) flushIfDue_(nowMs);
}

}  // namespace lamp
