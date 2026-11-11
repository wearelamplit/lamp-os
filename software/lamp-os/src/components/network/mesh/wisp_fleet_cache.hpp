#pragma once

#include <cstddef>
#include <cstdint>

namespace lamp {

/**
 * Accumulates MSG_WISP_CLAIM / MSG_WISP_STATE entries across frames.
 *        Per-entry upsert with staleness eviction rides out frames lost to
 *        coex. No locking of its own; LampRoster serializes access under its
 *        mutex.
 */
struct WispFleetCache {
  static constexpr size_t kCapacity = 100;
  // Entries older than this are evicted; pairing lasts <=60 s.
  static constexpr uint32_t kStaleMs = 60000;
  // Claims blob bytes per lamp: mac(6) + colorPair(6) + paintExt(3).
  static constexpr size_t kBlobEntrySize = 6 + 6 + 3;
  // Cap on a BLE characteristic value (the vendored ble_att.h limit). A larger
  // setValue reads back empty, so a direct-read blob is built within it.
  static constexpr size_t kAttValueMax = 512;

  struct ClaimEntry {
    uint8_t mac[6];
    uint32_t lastSeenMs;
    bool valid;
  };
  struct PaintEntry {
    uint8_t mac[6];
    uint8_t base[4];   // RGBW
    uint8_t shade[4];  // RGBW
    uint32_t lastSeenMs;
    bool valid;
  };

  ClaimEntry claims[kCapacity] = {};
  PaintEntry paints[kCapacity] = {};

  // `lampMacs` is [count][6] MAC bytes from one claim frame.
  void upsertClaims(const uint8_t lampMacs[][6], uint8_t count,
                    uint32_t nowMs);
  // One lamp's RGBW paint. With keepColors, a lamp already held fresh keeps
  // its colours and only refreshes its age.
  void upsertPaint(const uint8_t mac[6], const uint8_t base[4],
                   const uint8_t shade[4], uint32_t nowMs, bool keepColors);

  bool containsClaim(const uint8_t mac[6], uint32_t nowMs) const;
  // Fills baseOut/shadeOut (RGBW, 4 bytes each) on a fresh hit.
  bool findPaint(const uint8_t mac[6], uint32_t nowMs,
                 uint8_t baseOut[4], uint8_t shadeOut[4]) const;

  // [count:1][mac:6*count][baseRGB+shadeRGB:6*count]
  // [painted+baseW+shadeW:3*count]. Membership is the union of fresh claim
  // and fresh paint macs (claim order first). A mac with fresh paint has
  // painted = 1 and its colors, black included; one without has painted = 0
  // and zero colors. Entries beyond (outCap-1)/12 are dropped, and the paint
  // section is left off when it does not fit `outCap`, so a reader of the
  // [count][mac][pair] layout keeps its entry count. Returns bytes written.
  size_t buildClaimsBlob(uint8_t* out, size_t outCap, uint32_t nowMs) const;

  void clear();
  // Drops entries gone stale, so they cannot read fresh again one millis()
  // wrap later. Call at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs);
};

}  // namespace lamp
