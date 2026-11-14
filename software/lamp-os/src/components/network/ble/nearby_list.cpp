#include "components/network/ble/nearby_list.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "components/network/mesh/proximity.hpp"
#include "util/bd_addr.hpp"
#include "util/json_escape.hpp"

namespace ble_control {

namespace {

constexpr uint32_t kRebuildGapMs = 1000;
constexpr uint32_t kReadPollMs   = 5;
constexpr size_t   kHeapMargin   = 1024;
// Room past the measured length, so a rebuild whose numbers gained digits
// still fits the buffer held. A buffer this much over its list shrinks.
constexpr size_t kBuildSlack  = 128;
constexpr size_t kShrinkBytes = 2 * kBuildSlack;

void logThrottled(const char* what, size_t largest, size_t needed, size_t lamps) {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (lastMs != 0 && now - lastMs < 5000) return;
  lastMs = now == 0 ? 1 : now;
  Serial.printf("[ble_control] nearby %s: largest=%u needed=%u lamps=%u\n", what,
                static_cast<unsigned>(largest), static_cast<unsigned>(needed),
                static_cast<unsigned>(lamps));
}

void fnv1a(uint32_t& h, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (len--) { h ^= *p++; h *= 16777619u; }
}

void keepMostRelevant(std::vector<lamp::ListedPeer>& lamps, uint32_t now) {
  if (lamps.size() <= NearbyList::kMaxLamps) return;
  std::partial_sort(lamps.begin(), lamps.begin() + NearbyList::kMaxLamps, lamps.end(),
                    [now](const lamp::ListedPeer& a, const lamp::ListedPeer& b) {
                      return lamp::nearbyMoreRelevant(a, b, now, LAMP_PRUNE_TIME_MS);
                    });
  lamps.resize(NearbyList::kMaxLamps);
}

// Over the app-visible fields only. lastSeenMs and rssi freshen on every BLE
// adv (~30 Hz); folding them would churn the rev.
uint32_t membershipHash(const std::vector<lamp::ListedPeer>& lamps, uint32_t now) {
  uint32_t hash = 2166136261u;
  for (const auto& p : lamps) {
    const uint8_t flags =
        (p.lastSeenNearMs != 0 ? 1 : 0) | (p.lastSeenMeshMs != 0 ? 2 : 0) |
        (lamp::isNearNow(p.lastSeenNearMs, now, LAMP_PRUNE_TIME_MS) ? 4 : 0) |
        (p.hasMac ? 8 : 0) | (p.hasOtaSendingTo ? 16 : 0);
    fnv1a(hash, p.name, std::strlen(p.name));
    fnv1a(hash, &flags, 1);
    if (p.hasMac) fnv1a(hash, p.mac, 6);
    fnv1a(hash, &p.shadeColor, 4);
    fnv1a(hash, &p.baseColor, 4);
    fnv1a(hash, &p.firmwareVersion, sizeof(p.firmwareVersion));
    fnv1a(hash, p.fwChannel, std::strlen(p.fwChannel));
    fnv1a(hash, &p.otaState, 1);
    if (p.hasOtaSendingTo) fnv1a(hash, p.otaSendingTo, 6);
  }
  return hash;
}

// Counts what a render would write, so a build takes exactly that.
struct LengthCount {
  size_t n = 0;
  LengthCount& operator+=(char) { ++n; return *this; }
  LengthCount& operator+=(const char* s) { n += std::strlen(s); return *this; }
};

template <class Out>
void renderInto(const std::vector<lamp::ListedPeer>& lamps, uint32_t now, Out& out) {
  out += '[';
  char buf[96];
  bool first = true;
  for (const auto& p : lamps) {
    if (!first) out += ',';
    first = false;
    out += "{\"name\":\"";
    lamp::appendJsonEscaped(out, p.name);
    const uint32_t lastSeenMs = lamp::mostRecentSeenMs(p.lastSeenNearMs, p.lastSeenMeshMs, now);
    snprintf(buf, sizeof(buf), "\",\"lastSeenMs\":%lu,\"viaBle\":%s,\"viaEspNow\":%s,\"near\":%s",
             static_cast<unsigned long>(lastSeenMs),
             p.lastSeenNearMs != 0 ? "true" : "false",
             p.lastSeenMeshMs != 0 ? "true" : "false",
             lamp::isNearNow(p.lastSeenNearMs, now, LAMP_PRUNE_TIME_MS) ? "true" : "false");
    out += buf;
    if (p.hasMac) {
      char macBuf[18];
      lamp::formatBdAddr(p.mac, macBuf);
      out += ",\"lampId\":\"";
      out += macBuf;
      out += '"';
    }
    if (p.lastRssi != -127) {
      snprintf(buf, sizeof(buf), ",\"rssi\":%d", p.lastRssi);
      out += buf;
    }
    snprintf(buf, sizeof(buf),
             ",\"shade\":\"%02x%02x%02x%02x\",\"base\":\"%02x%02x%02x%02x\"",
             p.shadeColor.r, p.shadeColor.g, p.shadeColor.b, p.shadeColor.w,
             p.baseColor.r, p.baseColor.g, p.baseColor.b, p.baseColor.w);
    out += buf;
    if (p.firmwareVersion != 0) {
      snprintf(buf, sizeof(buf), ",\"fwVersion\":%lu",
               static_cast<unsigned long>(p.firmwareVersion));
      out += buf;
    }
    out += ",\"fwChannel\":\"";
    lamp::appendJsonEscapedAscii(out, p.fwChannel);
    out += '"';
    if (p.otaState != 0) {
      snprintf(buf, sizeof(buf), ",\"otaState\":%u", p.otaState);
      out += buf;
    }
    if (p.hasOtaSendingTo) {
      char macBuf[18];
      lamp::formatBdAddr(p.otaSendingTo, macBuf);
      out += ",\"otaSendingTo\":\"";
      out += macBuf;
      out += '"';
    }
    out += '}';
  }
  out += ']';
}

size_t renderedLength(const std::vector<lamp::ListedPeer>& lamps, uint32_t now) {
  LengthCount count;
  renderInto(lamps, now, count);
  return count.n;
}

}  // namespace

NearbyList::NearbyList() : mutex_(xSemaphoreCreateMutex()) {}

void NearbyList::render(const std::vector<lamp::ListedPeer>& lamps, uint32_t now,
                        std::string& out) {
  renderInto(lamps, now, out);
}

bool NearbyList::read(std::string& out, uint32_t nowMs) {
  readAtMs_.store(nowMs == 0 ? 1 : nowMs);
  for (uint32_t waited = 0; !built_.load() && waited < kReadWaitMs; waited += kReadPollMs) {
    vTaskDelay(pdMS_TO_TICKS(kReadPollMs));
  }
  return copy(out);
}

bool NearbyList::copy(std::string& out) {
  out.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool ok = built_.load();
  try {
    if (ok) out = json_;
  } catch (const std::bad_alloc&) {
    xSemaphoreGive(mutex_);
    throw;
  }
  xSemaphoreGive(mutex_);
  return ok;
}

bool NearbyList::wanted(bool clientConnected, uint32_t nowMs) {
  uint32_t at = readAtMs_.load();
  if (at == 0) return false;
  // Signed: a read on another core can stamp a time after the caller's nowMs.
  if (static_cast<int32_t>(nowMs - at) < static_cast<int32_t>(kHoldMs)) return clientConnected;
  // A failed exchange means a read restamped it since the load.
  return !readAtMs_.compare_exchange_strong(at, 0) && clientConnected;
}

void NearbyList::release(lamp::LampRoster& roster) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  built_.store(false);
  std::string().swap(json_);
  xSemaphoreGive(mutex_);
  roster.releaseAll();
  tried_   = false;
  holding_ = false;
}

bool NearbyList::tick(lamp::LampRoster& roster, bool clientConnected, uint32_t nowMs) {
  if (!wanted(clientConnected, nowMs)) {
    if (holding_) release(roster);
    return false;
  }
  const uint32_t gen = roster.generation();
  if (built_.load() && gen == builtGen_) return false;
  if (tried_ && nowMs - lastTryMs_ < kRebuildGapMs) return false;
  tried_     = true;
  lastTryMs_ = nowMs;
  return rebuild(roster, gen, nowMs);
}

bool NearbyList::rebuild(lamp::LampRoster& roster, uint32_t gen, uint32_t nowMs) {
  std::vector<lamp::ListedPeer>* all = nullptr;
  holding_ = true;
  try {
    all = &roster.getAll();
  } catch (const std::bad_alloc&) {
    logThrottled("roster OOM", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), 0, 0);
    return false;
  }
  auto& lamps = *all;
  keepMostRelevant(lamps, nowMs);
  std::sort(lamps.begin(), lamps.end(),
            [](const lamp::ListedPeer& a, const lamp::ListedPeer& b) {
              return std::strcmp(a.name, b.name) < 0;
            });
  const size_t length = renderedLength(lamps, nowMs);
  const uint32_t hash = membershipHash(lamps, nowMs);

  // Only the loop writes json_, so its capacity reads safely here unlocked.
  const size_t cap = json_.capacity();
  std::string sized;
  const bool resize = length > cap || cap - length >= kShrinkBytes;
  const size_t needed = length + kBuildSlack;
  if (resize) {
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < needed + kHeapMargin) {
      logThrottled("build skipped, low heap", largest, needed, lamps.size());
      return false;
    }
    try {
      sized.reserve(needed);
    } catch (const std::bad_alloc&) {
      logThrottled("OOM", largest, needed, lamps.size());
      return false;
    }
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (resize) json_.swap(sized);
  json_.clear();
  bool ok = true;
  try {
    render(lamps, nowMs, json_);
  } catch (const std::bad_alloc&) {
    ok = false;
  }
  if (!ok) std::string().swap(json_);
  const bool first = !built_.load();
  built_.store(ok);
  xSemaphoreGive(mutex_);
  if (!ok) {
    logThrottled("OOM", 0, needed, lamps.size());
    return false;
  }

  builtGen_ = gen;
  if (first || hash != lastHash_) {
    lastHash_ = hash;
    rev_.fetch_add(1);
    return true;
  }
  return false;
}

}  // namespace ble_control
