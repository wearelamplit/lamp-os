#pragma once

// Pure arithmetic for Moments (docs/dev/moments.md): pacing, the Fireflies
// timeline, peer selection and the `mo` wire object.
// No Arduino, no globals; the director and the native suite share it.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "components/network/mesh/lamp_roster.hpp"
#include "config/config_types.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

// Wire values of `k`.
enum class MomentKind : uint8_t { Fireflies = 1, Whisper = 2 };
inline constexpr size_t kMomentKindCount = 2;
inline constexpr size_t momentIndex(MomentKind k) { return static_cast<size_t>(k) - 1; }

inline constexpr uint8_t kDispositionNeutral = 3;
inline constexpr uint8_t kDispositionFond = 4;
inline constexpr uint8_t kDispositionSmitten = 5;

inline constexpr uint32_t kMinuteMs = 60000;
// A peer whose BLE advert was heard within this window is fresh, so its
// lastRssi is current. One never heard over BLE carries -127 dBm, below every
// threshold.
inline constexpr uint32_t kMomentFreshMs = 15000;

struct MomentPacing {
  uint16_t loMin;
  uint16_t hiMin;
  uint16_t stepMin;
  uint8_t growthCap;
  uint32_t crowdCooldownMs;
  uint32_t backoffMaxMs;
};
const MomentPacing& momentPacing(MomentKind kind);

// Percent applied to the lamp's own wait: an Extrovert starts Moments sooner.
uint32_t momentModePercent(SocialMode mode);
// The wait from now until the lamp is next eligible for `kind`.
uint32_t momentWaitMs(MomentKind kind, uint8_t growth, SocialMode mode, FastRng& rng);
uint8_t momentGrow(MomentKind kind, uint8_t growth);
uint32_t momentBackoffMs(MomentKind kind, FastRng& rng);

// murmur3 fmix32: a full-avalanche 32-bit mix.
uint32_t momentHash(uint32_t x);
uint32_t momentHashMac(const uint8_t mac[6]);
uint32_t macLow32(const uint8_t mac[6]);

// A stamp another task wrote just after the loop read the clock reads as up
// to this far in the future.
inline constexpr uint32_t kStampSkewMs = 1000;

// True when `stampMs` lies within `windowMs` before `nowMs`, or within the
// skew after it. A stamp of any other age, however old, is outside.
inline bool withinMs(uint32_t stampMs, uint32_t nowMs, uint32_t windowMs) {
  const uint32_t age = nowMs - stampMs;
  return age <= windowMs || age >= UINT32_MAX - kStampSkewMs;
}

inline bool isLiked(uint8_t disposition) { return disposition >= kDispositionFond; }
inline bool isFresh(const NearbyCopy& p, uint32_t nowMs) {
  return withinMs(p.lastSeenBleMs, nowMs, kMomentFreshMs);
}

// Below this summed RGBW a colour renders as near-black on a strip.
inline constexpr uint32_t kMomentMinColorEnergy = 24;
inline bool hasEnergy(Color c) {
  return static_cast<uint32_t>(c.r) + c.g + c.b + c.w >= kMomentMinColorEnergy;
}
inline bool isPeer(const NearbyCopy& p) { return p.name[0] != '\0' && p.hasMac; }

// count<<24 ^ xor(hash(mac)) over the liked peers in `peers`. Any arrival,
// departure or disposition change in the liked set changes it.
template <class Disposition>
uint32_t likedFingerprint(const std::vector<NearbyCopy>& peers, Disposition disposition,
                          uint8_t* likedCount = nullptr) {
  uint32_t count = 0;
  uint32_t mix = 0;
  for (const NearbyCopy& p : peers) {
    if (!isPeer(p) || !isLiked(disposition(p.mac))) continue;
    ++count;
    mix ^= momentHashMac(p.mac);
  }
  if (likedCount) *likedCount = count > 255 ? 255 : static_cast<uint8_t>(count);
  return (count << 24) ^ mix;
}

inline constexpr uint16_t kFirefliesBlinks = 120;
inline constexpr uint16_t kFirefliesScatterBlinks = 8;
inline constexpr int32_t kFirefliesNudgePermille = 60;
inline constexpr int32_t kFirefliesSnapMs = 10;

uint32_t firefliesPeriodMs(uint32_t seed);
// d_1 in [-T/2, T/2): this lamp's starting offset from the shared beat.
int32_t firefliesFirstOffset(uint32_t seed, const uint8_t mac[6]);
// d_{k+1} from d_k: held through the scatter blinks, then nudged toward zero.
int32_t firefliesNextOffset(int32_t offset, uint16_t blink);
// Blink k's time: t0 + k*T + d_k.
inline uint32_t firefliesBlinkMs(uint32_t t0, uint32_t periodMs, uint16_t blink,
                                 int32_t offset) {
  return t0 + static_cast<uint32_t>(blink) * periodMs + static_cast<uint32_t>(offset);
}
uint8_t firefliesOpacity(uint16_t blink);
// Stars lit by one blink on a surface of `surfacePixels`.
uint16_t firefliesStars(uint16_t surfacePixels);

// The fresh peer a Whisper may start toward: Smitten at or above -70 dBm, or
// Fond at or above -62 dBm.
template <class Disposition>
bool hasWhisperTarget(const std::vector<NearbyCopy>& peers, uint32_t nowMs,
                      Disposition disposition) {
  for (const NearbyCopy& p : peers) {
    if (!isPeer(p) || !isFresh(p, nowMs)) continue;
    const uint8_t d = disposition(p.mac);
    if ((d == kDispositionSmitten && p.lastRssi >= -70) ||
        (d == kDispositionFond && p.lastRssi >= -62)) {
      return true;
    }
  }
  return false;
}

inline constexpr uint8_t kWhisperMaxHops = 8;
inline constexpr int8_t kWhisperHopMinRssi = -80;

bool macIn(const uint8_t mac[6], const uint8_t (*list)[6], uint8_t count);
// Negative, zero or positive as `a` sorts before, with or after `b`.
int compareMac(const uint8_t a[6], const uint8_t b[6]);

// The next Whisper hop: fresh, liked by this lamp, at or above -80 dBm, not
// self and not visited; the strongest wins, then Smitten, then the lower MAC.
template <class Disposition>
bool selectWhisperHop(const std::vector<NearbyCopy>& peers, uint32_t nowMs,
                      const uint8_t self[6], const uint8_t (*visited)[6],
                      uint8_t visitedCount, Disposition disposition, uint8_t out[6]) {
  const NearbyCopy* best = nullptr;
  uint8_t bestDisposition = 0;
  for (const NearbyCopy& p : peers) {
    if (!isPeer(p) || !isFresh(p, nowMs) || p.lastRssi < kWhisperHopMinRssi) continue;
    if (compareMac(p.mac, self) == 0 || macIn(p.mac, visited, visitedCount)) continue;
    const uint8_t d = disposition(p.mac);
    if (!isLiked(d)) continue;
    const bool better =
        !best || p.lastRssi > best->lastRssi ||
        (p.lastRssi == best->lastRssi &&
         (d > bestDisposition ||
          (d == bestDisposition && compareMac(p.mac, best->mac) < 0)));
    if (better) {
      best = &p;
      bestDisposition = d;
    }
  }
  if (!best) return false;
  for (int i = 0; i < 6; ++i) out[i] = best->mac[i];
  return true;
}

inline constexpr uint32_t kMomentMaxAgeMs = 2000;

// The `mo` object of a MSG_EVENT Moment frame. `hop`, the MACs and the colour
// carry a Whisper only.
struct MomentFrame {
  MomentKind kind = MomentKind::Fireflies;
  uint32_t seed = 0;
  uint16_t ageMs = 0;
  uint8_t hop = 0;
  uint8_t to[6] = {0};
  uint8_t visited[kWhisperMaxHops][6] = {};
  // Whisper: the origin's colour, `c` on the wire.
  bool hasColor = false;
  Color color;
};

// Writes the whole MSG_EVENT payload, `{"type":"moment","mo":{...}}`, into
// `out`. Returns its length, or 0 when it does not fit `cap`.
size_t encodeMomentFrame(const MomentFrame& frame, char* out, size_t cap);
// Fills `out` from a received `mo` object. False for an unknown kind or any
// field out of range, which the receiver ignores.
bool decodeMomentFrame(JsonObjectConst mo, MomentFrame& out);

}  // namespace lamp
