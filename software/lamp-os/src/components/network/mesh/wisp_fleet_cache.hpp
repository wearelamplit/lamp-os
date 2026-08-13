#pragma once

#include <cstddef>
#include <cstdint>

namespace lamp {

/**
 * Accumulates MSG_WISP_CLAIM / MSG_WISP_PAINT entries across frames.
 *        The wisp rotates a partial window per 2 s beacon, so any single
 *        frame is an incomplete view; per-entry upsert with staleness
 *        eviction rebuilds full fleet coverage. Plain POD with no locking;
 *        LampRoster serialises access under its mutex.
 */
struct WispFleetCache {
  static constexpr size_t kCapacity = 100;
  // Entries older than this are evicted; pairing lasts <=60 s.
  static constexpr uint32_t kStaleMs = 60000;

  struct ClaimEntry {
    uint8_t mac[6];
    uint32_t lastSeenMs;
    bool valid;
  };
  struct PaintEntry {
    uint8_t mac[6];
    uint8_t base[3];
    uint8_t shade[3];
    uint32_t lastSeenMs;
    bool valid;
  };

  ClaimEntry claims[kCapacity] = {};
  PaintEntry paints[kCapacity] = {};

  // `lampMacs` is [count][6] MAC bytes from one claim frame.
  void upsertClaims(const uint8_t lampMacs[][6], uint8_t count,
                    uint32_t nowMs);
  // `entries` is count * 12 bytes: lampMac(6)+baseRGB(3)+shadeRGB(3).
  void upsertPaints(const uint8_t* entries, uint8_t count, uint32_t nowMs);

  bool containsClaim(const uint8_t mac[6], uint32_t nowMs) const;
  // Fills baseOut/shadeOut (3 bytes each) on a fresh hit.
  bool findPaint(const uint8_t mac[6], uint32_t nowMs,
                 uint8_t baseOut[3], uint8_t shadeOut[3]) const;

  // [count:1][mac:6*count][baseRGB+shadeRGB:6*count]. Membership is the
  // union of fresh claim and fresh paint macs (claim order first); color
  // pair is 00*6 for a mac with no fresh paint. Entries beyond
  // (outCap-1)/12 are dropped. Returns bytes written.
  size_t buildClaimsBlob(uint8_t* out, size_t outCap, uint32_t nowMs) const;

  void clear();
};

}  // namespace lamp
