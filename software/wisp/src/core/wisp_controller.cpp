#include "core/wisp_controller.hpp"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "aurora/AuroraPaletteClient.h"
#include "config/wisp_config.hpp"
#include "config/zone_selector.hpp"
#include "paint/paint_distributor.hpp"
#include "artnet/artnet_emitter.hpp"
#include "status/status_emitter.hpp"
#include "status/status_ring.hpp"

namespace wisp {

// Empty palette deliberately skips the update so flipping Manual -> empty
// doesn't zero the lamps' fallback color.
void WispController::pushManualPaletteToCurrent() {
  const auto& cols = config_.manualPalette();
  if (cols.empty()) return;
  Palette p;
  p.id = "manual";
  p.hexColors.reserve(cols.size());
  for (const auto& c : cols) {
    const uint64_t packed = (static_cast<uint64_t>(c.r) << 16) |
                            (static_cast<uint64_t>(c.g) << 8) |
                            (static_cast<uint64_t>(c.b));
    p.hexColors.push_back(packed);
  }
  palette_.update(p, millis());
  paint_.onPaletteChanged();
  artnet_.onPaletteChanged();
}

// Event-driven; never called from loop() steady state. Stack-only.
void WispController::renderRing() {
  uint8_t stopsRgb[wisp::kManualPaletteMaxColors * 3];
  size_t numStops = 0;
  uint8_t pixels[wisp::kStatusRingPixelCount * 3];

  const wisp::WispSourceMode mode = config_.sourceMode();
  // Render as Off when Aurora stream is down, not stale palette.
  const bool auroraLive =
      mode == wisp::WispSourceMode::Aurora && aurora_.isStreaming();
  if (mode == wisp::WispSourceMode::Manual) {
    const auto& cols = config_.manualPalette();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      stopsRgb[i * 3 + 0] = cols[i].r;
      stopsRgb[i * 3 + 1] = cols[i].g;
      stopsRgb[i * 3 + 2] = cols[i].b;
    }
    numStops = n;
  } else if (auroraLive) {
    const auto& cols = palette_.colors();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      // WS2812 ring has no W channel; fold W into R/G with warm bias.
      wisp::rgbwToRgbWarmBias(cols[i].r, cols[i].g, cols[i].b, cols[i].w,
                              stopsRgb[i * 3 + 0], stopsRgb[i * 3 + 1],
                              stopsRgb[i * 3 + 2]);
    }
    numStops = n;
  }
  // Off and Aurora-with-no-stream show the operator off-color.
  // Manual with empty palette falls through to warm-white.
  if (mode == wisp::WispSourceMode::Off ||
      (mode == wisp::WispSourceMode::Aurora && !auroraLive)) {
    const wisp::ManualPaletteColor offColor = config_.offColor();
    for (size_t i = 0; i < wisp::kStatusRingPixelCount; ++i) {
      pixels[i * 3 + 0] = offColor.r;
      pixels[i * 3 + 1] = offColor.g;
      pixels[i * 3 + 2] = offColor.b;
    }
  } else if (!wisp::computeRingGradient(stopsRgb, numStops, pixels,
                                        wisp::kStatusRingPixelCount)) {
    wisp::fillRingWarmWhite(pixels, wisp::kStatusRingPixelCount);
  }

  for (size_t i = 0; i < wisp::kStatusRingPixelCount; ++i) {
    strip_.setPixelColor(static_cast<uint16_t>(i),
                         pixels[i * 3 + 0],
                         pixels[i * 3 + 1],
                         pixels[i * 3 + 2]);
  }
  strip_.show();
}

void WispController::applySourceModeTransition(wisp::WispSourceMode mode) {
  switch (mode) {
    case wisp::WispSourceMode::Off:
      paint_.setPaintMode(false);
      Serial.println("[wisp] source=Off — broadcast RESTORE; paintMode off");
      break;
    case wisp::WispSourceMode::Manual:
      paint_.setPaintMode(true);
      pushManualPaletteToCurrent();
      Serial.println("[wisp] source=Manual — paintMode on");
      break;
    case wisp::WispSourceMode::Aurora:
      // Clear stale palette; onAuroraPalette enables paint when a live
      // palette arrives; loop's liveness check disables it if stream drops.
      palette_.clear();
      paint_.setPaintMode(aurora_.isStreaming());
      Serial.printf("[wisp] source=Aurora — %s\n",
                    aurora_.isStreaming() ? "stream live"
                                          : "no stream, holding off");
      break;
  }
  renderRing();
}

void WispController::onAuroraPalette(int zone, const Palette& p) {
  zones_.observe(zone);

  // First-seen latch: only when neither NVS nor an app op has chosen a zone.
  if (zones_.latchFirstSeen(zone)) {
    Serial.printf("[wisp] claimed Aurora zone %d (source=firstSeen)\n", zone);
    status_.triggerOnChange();
  }

  if (zone != zones_.currentZone()) {
    Serial.printf("[wisp] ignoring zone %d palette (selected %d, source=%s)\n",
                  zone, zones_.currentZone(),
                  wisp::zoneSourceName(zones_.source()));
    return;
  }

  // Only Aurora mode lets these callbacks drive CurrentPalette; Manual/Off
  // would have their palette overwritten by the next Aurora notify.
  if (config_.sourceMode() != wisp::WispSourceMode::Aurora) {
    return;
  }

  palette_.update(p, millis());
  const auto& cols = palette_.colors();
  Serial.printf("[wisp] palette change: %s with %u colors\n",
                palette_.paletteId().c_str(),
                (unsigned)cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    Serial.printf("  [%u] r=%u g=%u b=%u w=%u\n",
                  (unsigned)i, cols[i].r, cols[i].g, cols[i].b, cols[i].w);
  }
  paint_.setPaintMode(true);
  artnet_.onPaletteChanged();
  renderRing();
}

void WispController::applyOpResult(DispatchResult res) {
  switch (res) {
    case wisp::DispatchResult::AppliedSourceChange: {
      applySourceModeTransition(config_.sourceMode());
      status_.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedManualPalette: {
      if (config_.sourceMode() == wisp::WispSourceMode::Manual) {
        pushManualPaletteToCurrent();
        renderRing();
      }
      status_.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedZoneChange: {
      if (config_.hasSelectedZone()) {
        const int newZone = config_.selectedZone();
        zones_.setFromOp(newZone);
        Serial.printf("[wisp] zone set by app op to %d (source=appOp)\n",
                      newZone);
      } else {
        zones_.clearFromOp();
        Serial.println("[wisp] zone cleared by app op (source=none)");
      }
      status_.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedWifiChange:
      Serial.println("[wisp] wifi creds updated; STA reconnect + advert refresh kicked");
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedOffColor:
      if (config_.sourceMode() == wisp::WispSourceMode::Off) {
        renderRing();
      }
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedShuffle:
      paint_.setShuffleSeed(config_.shuffleSeed());
      paint_.onPaletteChanged();
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::Ignored:
    case wisp::DispatchResult::Malformed:
      break;
  }
}

void WispController::tickAuroraLiveness() {
  // Edge-triggered: stream drop -> RESTORE walk; onAuroraPalette re-enables
  // paint when a fresh palette arrives.
  if (config_.sourceMode() != wisp::WispSourceMode::Aurora) return;
  const bool streaming = aurora_.isStreaming();
  if (auroraWasStreaming_ && !streaming) {
    palette_.clear();
    paint_.setPaintMode(false);
    renderRing();
    status_.triggerOnChange();
    Serial.println("[wisp] Aurora stream dropped — holding off");
  }
  auroraWasStreaming_ = streaming;
}

}  // namespace wisp
