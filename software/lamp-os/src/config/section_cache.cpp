#include "config/section_cache.hpp"

#include <Arduino.h>

#include <new>

#include "util/uptime.hpp"

namespace lamp {

// The heap check before each of these cannot reserve the block: another
// task can take it first. The catch is the last line, best effort since the
// build has no exception emergency pool.
static bool assignNoThrow(std::string& dst, const char* src, size_t len) {
  try {
    dst.assign(src, len);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

// The document serializes into one guarded buffer: an Arduino String grown on
// a fragmented heap silently stops appending and would cache a cut-off JSON as
// valid.
void SectionCache::store(const JsonDocument& doc, const char* name) {
  size_t len = 0;
  const auto json = serializeGuarded(doc, len);
  if (json && (json_.capacity() >= len || heapCanSpare(len)) &&
      assignNoThrow(json_, json.get(), len)) {
    valid_ = true;
  } else {
    fail(name);
  }
}

// The section stays dirty so the next read retries once the heap recovers.
void SectionCache::fail(const char* name) {
  valid_ = false;
  dirty_.store(true);
  logOom(name);
}

bool SectionCache::copyOut(std::string& out, const char* name) const {
  if (valid_ && (out.capacity() >= json_.size() || heapCanSpare(json_.size())) &&
      assignNoThrow(out, json_.data(), json_.size())) {
    return true;
  }
  out.clear();
  if (valid_) logOom(name);
  return false;
}

void SectionCache::release() {
  std::string().swap(json_);
  valid_ = false;
  dirty_.store(true);
}

// A field OOM must stay visible without spamming: warn on every channel,
// throttled to one line per kMinGapMs across all sections.
void SectionCache::logOom(const char* name) {
  static constexpr uint64_t kMinGapMs = 5000;
  static uint64_t lastMs = 0;
  const uint64_t now = uptimeMs();
  if (now - lastMs < kMinGapMs && lastMs != 0) return;
  lastMs = now;
  Serial.printf("[cfg] %s section OOM\n", name);
}

}  // namespace lamp
