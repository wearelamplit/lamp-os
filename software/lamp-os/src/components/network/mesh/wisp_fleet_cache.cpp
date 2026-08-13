#include "wisp_fleet_cache.hpp"

#include <cstring>

namespace lamp {

namespace {

bool fresh(bool valid, uint32_t lastSeenMs, uint32_t nowMs) {
  return valid && (nowMs - lastSeenMs) <= WispFleetCache::kStaleMs;
}

// Slot for `mac`: an existing fresh match, else the first stale slot, else
// the oldest entry. Entry layout differs between the two arrays, so this is
// generic over the entry type.
template <typename Entry, size_t N>
Entry& slotFor(Entry (&entries)[N], const uint8_t mac[6], uint32_t nowMs) {
  for (Entry& e : entries) {
    if (fresh(e.valid, e.lastSeenMs, nowMs) &&
        std::memcmp(e.mac, mac, 6) == 0) {
      return e;
    }
  }
  for (Entry& e : entries) {
    if (!fresh(e.valid, e.lastSeenMs, nowMs)) return e;
  }
  size_t oldest = 0;
  for (size_t i = 1; i < N; ++i) {
    if (nowMs - entries[i].lastSeenMs > nowMs - entries[oldest].lastSeenMs) {
      oldest = i;
    }
  }
  return entries[oldest];
}

}  // namespace

void WispFleetCache::upsertClaims(const uint8_t lampMacs[][6], uint8_t count,
                                  uint32_t nowMs) {
  if (!lampMacs) return;
  for (uint8_t i = 0; i < count; ++i) {
    ClaimEntry& e = slotFor(claims, lampMacs[i], nowMs);
    std::memcpy(e.mac, lampMacs[i], 6);
    e.lastSeenMs = nowMs;
    e.valid = true;
  }
}

void WispFleetCache::upsertPaints(const uint8_t* entries, uint8_t count,
                                  uint32_t nowMs) {
  if (!entries) return;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* src = entries + static_cast<size_t>(i) * 12;
    PaintEntry& e = slotFor(paints, src, nowMs);
    std::memcpy(e.mac, src, 6);
    std::memcpy(e.base, src + 6, 3);
    std::memcpy(e.shade, src + 9, 3);
    e.lastSeenMs = nowMs;
    e.valid = true;
  }
}

bool WispFleetCache::containsClaim(const uint8_t mac[6],
                                   uint32_t nowMs) const {
  for (const ClaimEntry& e : claims) {
    if (fresh(e.valid, e.lastSeenMs, nowMs) &&
        std::memcmp(e.mac, mac, 6) == 0) {
      return true;
    }
  }
  return false;
}

bool WispFleetCache::findPaint(const uint8_t mac[6], uint32_t nowMs,
                               uint8_t baseOut[3], uint8_t shadeOut[3]) const {
  for (const PaintEntry& e : paints) {
    if (fresh(e.valid, e.lastSeenMs, nowMs) &&
        std::memcmp(e.mac, mac, 6) == 0) {
      std::memcpy(baseOut, e.base, 3);
      std::memcpy(shadeOut, e.shade, 3);
      return true;
    }
  }
  return false;
}

size_t WispFleetCache::buildClaimsBlob(uint8_t* out, size_t outCap,
                                       uint32_t nowMs) const {
  if (!out || outCap == 0) return 0;
  const size_t maxEntries = (outCap - 1) / 12;

  const uint8_t* macs[2 * kCapacity];
  size_t n = 0;
  for (const ClaimEntry& e : claims) {
    if (n >= maxEntries || n >= 2 * kCapacity) break;
    if (fresh(e.valid, e.lastSeenMs, nowMs)) macs[n++] = e.mac;
  }
  for (const PaintEntry& e : paints) {
    if (n >= maxEntries || n >= 2 * kCapacity) break;
    if (fresh(e.valid, e.lastSeenMs, nowMs) &&
        !containsClaim(e.mac, nowMs)) {
      macs[n++] = e.mac;
    }
  }

  out[0] = static_cast<uint8_t>(n);
  for (size_t i = 0; i < n; ++i) {
    std::memcpy(out + 1 + i * 6, macs[i], 6);
  }
  const size_t colorBase = 1 + n * 6;
  for (size_t i = 0; i < n; ++i) {
    uint8_t* dst = out + colorBase + i * 6;
    uint8_t base[3], shade[3];
    if (findPaint(macs[i], nowMs, base, shade)) {
      std::memcpy(dst, base, 3);
      std::memcpy(dst + 3, shade, 3);
    } else {
      std::memset(dst, 0, 6);
    }
  }
  return 1 + n * 12;
}

void WispFleetCache::clear() {
  for (ClaimEntry& e : claims) e.valid = false;
  for (PaintEntry& e : paints) e.valid = false;
}

}  // namespace lamp
