#include "ota_indicator.hpp"

#include <math.h>

#include <cstdint>
#include <cstring>

#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/network/nearby_lamps.hpp"
#include "core/frame_buffer.hpp"
#include "util/color.hpp"

// `firmwareReceiver` lives at file scope in lamp.cpp (matches the original
// pre-namespace pattern); `lamp::firmwareDistributor` is in the lamp
// namespace (declared via extern in firmware_distributor.hpp). Pulling
// lamp_internal.hpp would be inappropriate — it's documented as private to
// lamp.cpp + lamp_drains.cpp — so we forward-declare the receiver extern
// here in the same shape.
extern lamp::FirmwareReceiver firmwareReceiver;

namespace lamp {
namespace ota_indicator {

namespace {

// Per-channel scale by a 0..255 numerator: out = in * num / 255.
inline Color scaleColor(const Color& c, uint8_t num) {
  return Color(
      static_cast<uint8_t>((static_cast<uint16_t>(c.r) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.g) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.b) * num) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(c.w) * num) / 255));
}

// 20% scale (51/255). The dim background uses this — kept low so the
// peer-color band on top reads as a distinct foreground.
constexpr uint8_t kDimScale255 = 51;

// No pulse — earlier versions used a 20%↔100% then 60%↔100% sine to
// signal "I'm alive". The bar growing left-to-right is the live signal; a
// brightness oscillation on top of it is redundant.

}  // namespace

void paint(FrameBuffer* fb, const Color& localBase, uint32_t nowMs) {
  if (fb == nullptr) return;
  const size_t pixelCount = fb->buffer.size();
  if (pixelCount == 0) return;

  // Probe the in-flight OTA side. Receiver wins ties (shouldn't happen —
  // the cross-state-machine guard in lamp.cpp prevents simultaneous
  // distribute + receive — but defensively pick one).
  bool haveSession = false;
  uint8_t peerMac[6] = {0};
  uint32_t done = 0;
  uint32_t total = 0;

  // Inter-session hold flag: when the distributor finishes one session
  // in a multi-peer OTA wave it holds quiet for a few seconds so the
  // strip doesn't flash back to normal animations between sessions.
  // During that hold there's no live session — pin the bar at 100% of
  // the just-completed peer's color so visually we read as "we just
  // finished sending to X" until the next session takes over.
  bool inHoldRender = false;

  if (::firmwareReceiver.isInProgress() &&
      ::firmwareReceiver.getPeerMac(peerMac)) {
    haveSession = true;
    done = ::firmwareReceiver.recvChunksCount();
    total = ::firmwareReceiver.totalChunks();
  } else if (lamp::firmwareDistributor.isInProgress() &&
             lamp::firmwareDistributor.getPeerMac(peerMac)) {
    haveSession = true;
    done = lamp::firmwareDistributor.sentChunksCount();
    total = lamp::firmwareDistributor.totalChunks();
  } else {
    // No live session — see if the distributor is in the inter-session
    // hold window. If so, render as if the just-completed session is at
    // 100% (done=total) so the bar visually freezes at the end of the
    // prior peer's progress through the gap to the next session.
    uint16_t txHoldTotal = 0;
    if (lamp::firmwareDistributor.getLastSession(peerMac, txHoldTotal) &&
        txHoldTotal > 0) {
      haveSession  = true;
      inHoldRender = true;
      total        = txHoldTotal;
      done         = txHoldTotal;  // pin to 100%
    }
  }

  // No session — write the dim background only. The compositor's caller
  // gated on ota_quiet_mode::isQuiet() so this branch should be rare
  // (transition window between quiet-mode entry and the state machine
  // populating its fields).
  const Color dim = scaleColor(localBase, kDimScale255);
  for (size_t i = 0; i < pixelCount; i++) {
    fb->buffer[i] = dim;
  }

#ifdef LAMP_DEBUG
  // Per-tick flicker diagnostic: log which branch fired and the live
  // state probes. Helps catch isInProgress()/totalChunks() flapping
  // mid-session — the symptom is the strip alternating between
  // progress bar and dim-only fill. Logs once per indicator paint
  // call (driven by compositor's flush cadence); the static-state
  // dedupe just prints branch transitions to keep the volume sane.
  static const char* lastBranch = nullptr;
  static uint32_t   lastSameMs  = 0;
  const char* branch;
  if (!haveSession) branch = "NO-SESSION (dim only)";
  else if (total == 0) branch = "HAVE-SESSION total=0 (dim only)";
  else if (inHoldRender) branch = "HOLD-RENDER";
  else branch = "LIVE";
  if (branch != lastBranch || (nowMs - lastSameMs) >= 1000) {
    Serial.printf("[ota_ind] t=%u br=%s rxInProgress=%d rxTotal=%u "
                  "txInProgress=%d txTotal=%u done=%u total=%u\n",
                  (unsigned)nowMs, branch,
                  (int)::firmwareReceiver.isInProgress(),
                  (unsigned)::firmwareReceiver.totalChunks(),
                  (int)lamp::firmwareDistributor.isInProgress(),
                  (unsigned)lamp::firmwareDistributor.totalChunks(),
                  (unsigned)done, (unsigned)total);
    lastBranch = branch;
    lastSameMs = nowMs;
  }
#endif

  if (!haveSession || total == 0) return;
  (void)inHoldRender;  // currently identical render path; future visual
                       // tweak (e.g. a subtle dim-down) can use this flag.

  // Session-stable peer color. The bar paints in the SENDER's base color so
  // the strip reads as "who's flashing me". findByMac can miss — NearbyLamps
  // is cold right after a flash, and HELLO processing is starved while the
  // lamp sits in OTA quiet mode — so resolving every frame made the color
  // oscillate between the sender's color and the fallback, which IS the
  // flicker. Resolve ONCE per session: latch the first hit and hold it; until
  // then show the lamp's OWN base color (stable), never a jarring white.
  static uint8_t s_latchMac[6] = {0};
  static bool    s_latched     = false;
  static Color   s_peerColor;
  if (std::memcmp(s_latchMac, peerMac, 6) != 0) {
    std::memcpy(s_latchMac, peerMac, 6);
    s_latched = false;
  }
  if (!s_latched) {
    NearbyLamp peer;
    if (nearbyLamps.findByMac(peerMac, peer)) {
      s_peerColor = peer.baseColor;
      s_latched   = true;
    }
  }
  const Color peerSolid = s_latched ? s_peerColor : localBase;

  // Clamp done at total — if the wisp/sender races ahead of the receiver
  // briefly we don't want progress > 100%.
  if (done > total) done = total;

  // Sub-pixel progress in fixed 8.8 fractional form. wholePixels is the
  // count of fully-peer-color pixels; fracEdge255 is the fractional part
  // of the boundary pixel, 0..255. Without this the boundary pixel snaps
  // hard from dim→peer once per ~total/pixelCount chunks, which reads
  // as a flicker; with anti-alias it ramps smoothly.
  const uint64_t scaledProgress =
      (static_cast<uint64_t>(pixelCount) * static_cast<uint64_t>(done) * 256u) /
      static_cast<uint64_t>(total);
  const size_t wholePixels = static_cast<size_t>(scaledProgress >> 8);
  const uint8_t fracEdge255 = static_cast<uint8_t>(scaledProgress & 0xFFu);

  for (size_t i = 0; i < wholePixels && i < pixelCount; i++) {
    fb->buffer[i] = peerSolid;
  }
  // Edge pixel — blend dim background and peer color by the fractional
  // edge amount. fracEdge255=0 keeps dim; 255 hits peerSolid.
  if (wholePixels < pixelCount) {
    const Color& a = dim;
    const Color& b = peerSolid;
    const uint8_t f = fracEdge255;
    const uint8_t inv = static_cast<uint8_t>(255u - f);
    fb->buffer[wholePixels] = Color(
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.r) * inv +
             static_cast<uint16_t>(b.r) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.g) * inv +
             static_cast<uint16_t>(b.g) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.b) * inv +
             static_cast<uint16_t>(b.b) * f) / 255),
        static_cast<uint8_t>(
            (static_cast<uint16_t>(a.w) * inv +
             static_cast<uint16_t>(b.w) * f) / 255));
  }

#ifdef LAMP_DEBUG
  // Rate-limited paint diagnostic — log what the indicator is actually
  // computing about every 500 ms so we can correlate visible flicker
  // with the internal state. Logs once across both frame buffers; the
  // tracking variable is function-static, scoped to the FIRST FB call
  // each render cycle.
  static uint32_t lastLogMs = 0;
  if (nowMs - lastLogMs > 500) {
    lastLogMs = nowMs;
    Serial.printf("[ota_ind] done=%u total=%u whole=%u frac=%u "
                  "dim=(%u,%u,%u,%u) peer=(%u,%u,%u,%u) "
                  "px0=(%u,%u,%u,%u) pxEdge=(%u,%u,%u,%u)\n",
                  (unsigned)done, (unsigned)total,
                  (unsigned)wholePixels, (unsigned)fracEdge255,
                  dim.r, dim.g, dim.b, dim.w,
                  peerSolid.r, peerSolid.g, peerSolid.b, peerSolid.w,
                  fb->buffer[0].r, fb->buffer[0].g, fb->buffer[0].b, fb->buffer[0].w,
                  fb->buffer[wholePixels < pixelCount ? wholePixels : 0].r,
                  fb->buffer[wholePixels < pixelCount ? wholePixels : 0].g,
                  fb->buffer[wholePixels < pixelCount ? wholePixels : 0].b,
                  fb->buffer[wholePixels < pixelCount ? wholePixels : 0].w);
  }
#endif
}

}  // namespace ota_indicator
}  // namespace lamp
