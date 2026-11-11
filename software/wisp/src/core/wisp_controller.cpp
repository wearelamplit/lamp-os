#include "core/wisp_controller.hpp"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <cstdio>
#include <cstring>
#include <lampos/led_types.hpp>

#include "config/wisp_config.hpp"
#include "paint/paint_distributor.hpp"
#include "paint/palette_id.hpp"
#include "artnet/artnet_emitter.hpp"
#include "status/status_emitter.hpp"
#include "status/status_ring.hpp"
#include "util/fnv1a.hpp"

namespace wisp {

// An empty palette pushes nothing; the caller has already marked the source
// not-live so lamps ease home. The caller renders the ring and then emits
// ArtNet, in that order, since the ring render opens the ArtNet gate.
void WispController::pushManualPaletteToCurrent() {
  const auto& cols = config_.manualPalette();
  if (cols.empty()) return;
  Palette p;
  // Content hash so the app confirms this exact palette on the status NOTIFY.
  p.id = manualPaletteId(cols);
  p.colors.reserve(cols.size());
  for (const auto& c : cols) {
    PaletteColor pc;
    pc.r = c.r / 255.0f;
    pc.g = c.g / 255.0f;
    pc.b = c.b / 255.0f;
    pc.w = c.w / 255.0f;
    p.colors.push_back(pc);
  }
  palette_.update(p, millis());
  paint_.onPaletteChanged();
}

bool WispController::sourceLive() const {
  return config_.sourceMode() == wisp::WispSourceMode::Osc && oscFresh_.live();
}

bool WispController::effectiveOff() const {
  const wisp::WispSourceMode mode = config_.sourceMode();
  return mode == wisp::WispSourceMode::Off ||
         (mode == wisp::WispSourceMode::Osc && !oscFresh_.live());
}

// Fill `pixels` (pre-gamma RGB, length px*3) from the current source state.
void WispController::computeRingPixels(uint8_t* pixels, size_t px, bool off) {
  uint8_t stopsRgb[wisp::kManualPaletteMaxColors * 3];
  size_t numStops = 0;

  const wisp::WispSourceMode mode = config_.sourceMode();
  if (mode == wisp::WispSourceMode::Manual) {
    const auto& cols = config_.manualPalette();
    const size_t n = cols.size() > wisp::kManualPaletteMaxColors
                         ? wisp::kManualPaletteMaxColors
                         : cols.size();
    for (size_t i = 0; i < n; ++i) {
      wisp::rgbwToRgbWarmBias(cols[i].r, cols[i].g, cols[i].b, cols[i].w,
                              stopsRgb[i * 3 + 0], stopsRgb[i * 3 + 1],
                              stopsRgb[i * 3 + 2]);
    }
    numStops = n;
  } else if (sourceLive()) {
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
  // Off and Osc-with-a-stale-stream show the off-color.
  // Manual with empty palette falls through to warm-white.
  if (off) {
    const wisp::ManualPaletteColor offColor = config_.offColor();
    uint8_t offR, offG, offB;
    wisp::rgbwToRgbWarmBias(offColor.r, offColor.g, offColor.b, offColor.w,
                            offR, offG, offB);
    for (size_t i = 0; i < px; ++i) {
      pixels[i * 3 + 0] = offR;
      pixels[i * 3 + 1] = offG;
      pixels[i * 3 + 2] = offB;
    }
  } else if (!wisp::computeRingGradient(stopsRgb, numStops, pixels, px)) {
    wisp::fillRingWarmWhite(pixels, px);
  }
}

void WispController::showPixels(const uint8_t* pixels, size_t px) {
  for (size_t i = 0; i < px; ++i) {
    strip_.setPixelColor(static_cast<uint16_t>(i),
                         Adafruit_NeoPixel::gamma8(pixels[i * 3 + 0]),
                         Adafruit_NeoPixel::gamma8(pixels[i * 3 + 1]),
                         Adafruit_NeoPixel::gamma8(pixels[i * 3 + 2]));
  }
  strip_.show();
  std::memcpy(shown_, pixels, px * 3);
  shownPx_ = px;
  ringReady_ = true;
}

// Event-driven; never called from loop() steady state. Stack-only. Only the
// Off<->painting flip crossfades; other changes (palette edit, resize) snap.
void WispController::renderRing() {
  const size_t px = (config_.pixelCount() <= wisp::kMaxRingPixels)
                        ? config_.pixelCount()
                        : wisp::kMaxRingPixels;
  const bool off = effectiveOff();
  // Old lamps get released on ArtNet the same way mesh lamps are released
  // (isWispActive=0): no frames at all, rather than being driven to the
  // off-color or, with no palette, to black. Stays immediate; only the local
  // ring fades.
  artnet_.setActive(!off && !palette_.colors().empty());

  uint8_t target[wisp::kMaxRingPixels * 3];
  computeRingPixels(target, px, off);

  if (ringReady_ && off != lastOff_ && px == shownPx_) {
    std::memcpy(fadeFrom_, shown_, px * 3);
    std::memcpy(fadeTo_, target, px * 3);
    fadePx_ = px;
    fadeStartMs_ = millis();
    fading_ = true;
  } else {
    fading_ = false;
    showPixels(target, px);
  }
  lastOff_ = off;
}

void WispController::tickRingFade(uint32_t now) {
  if (!fading_) return;
  // renderRing stamps fadeStartMs_ with millis(), which can be later than the
  // loop-start `now`; that reads as no time elapsed rather than a wrap.
  const int32_t dt = static_cast<int32_t>(now - fadeStartMs_);
  const uint32_t elapsed = dt < 0 ? 0 : static_cast<uint32_t>(dt);
  const uint8_t w = wisp::ringFadeWeight(elapsed, wisp::kRingFadeMs);
  uint8_t frame[wisp::kMaxRingPixels * 3];
  wisp::lerpRing(fadeFrom_, fadeTo_, frame, fadePx_ * 3, w);
  showPixels(frame, fadePx_);
  if (w == 255) fading_ = false;
}

void WispController::applyLedConfig() {
  strip_.updateType(lampos::led::neoPixelFormat(config_.ledFormat()) +
                    NEO_KHZ800);
  strip_.updateLength(config_.pixelCount());
  strip_.begin();
  strip_.clear();
  renderRing();
}

void WispController::resetOscStream() {
  oscFresh_.reset();
  oscPaintGate_.reset();
  oscLastFloats_ = 0;
  palette_.clear();
}

void WispController::holdOscStream() {
  resetOscStream();
  paint_.setSourceLive(false);
  renderRing();
  status_.requestEmit();
}

void WispController::pushOscPaint() {
  paint_.onPaletteChanged();
  artnet_.onPaletteChanged();
}

void WispController::applySourceModeTransition(wisp::WispSourceMode mode) {
  // Re-selecting Osc (an app resend after a lost confirm, or src:osc) must
  // not drop a live stream.
  if (mode == wisp::WispSourceMode::Osc && haveAppliedMode_ &&
      appliedMode_ == mode && oscFresh_.live()) {
    paint_.setPaintMode(true);
    renderRing();
    return;
  }
  haveAppliedMode_ = true;
  appliedMode_ = mode;
  switch (mode) {
    case wisp::WispSourceMode::Off:
      paint_.setPaintMode(false);
      paint_.setSourceLive(true);
      // Off plays nothing; a stale paletteIdPrefix would keep triggering
      // app-side palette re-reads.
      palette_.clear();
      Serial.println("[wisp] source=Off — paintMode off, STATE clears paint");
      break;
    case wisp::WispSourceMode::Manual:
      // An empty manual palette paints nothing; clearing drops whatever the
      // previous source left behind so lamps ease home instead.
      palette_.clear();
      paint_.setSourceLive(!config_.manualPalette().empty());
      paint_.setPaintMode(true);
      pushManualPaletteToCurrent();
      Serial.println("[wisp] source=Manual — paintMode on");
      break;
    case wisp::WispSourceMode::Osc:
      // Choosing Osc is the paint opt-in. Lamps stay at Off until the first
      // palette makes the stream live.
      resetOscStream();
      paint_.setSourceLive(false);
      paint_.setPaintMode(true);
      Serial.printf("[wisp] source=Osc — paintMode on, waiting for %s\n",
                    oscPaletteAddress(config_.oscRole()));
      break;
  }
  renderRing();
  if (mode == wisp::WispSourceMode::Manual) artnet_.onPaletteChanged();
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
        if (config_.manualPalette().empty()) palette_.clear();
        paint_.setSourceLive(!config_.manualPalette().empty());
        pushManualPaletteToCurrent();
        renderRing();
        artnet_.onPaletteChanged();
      }
      status_.triggerOnChange();
      break;
    }
    case wisp::DispatchResult::AppliedOscRole:
      // Lamps keep the old role's palette until the new role's first one
      // lands; onOscPalette keys its dedup on the role.
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedWifiChange:
      Serial.println("[wisp] wifi creds updated; STA reconnect + advert refresh kicked");
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedOffColor:
      if (effectiveOff()) {
        renderRing();
      }
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedShuffle:
      paint_.setShuffleSeed(config_.shuffleSeed());
      paint_.onPaletteChanged();
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedDriftChange:
      paint_.setDrift(config_.driftIntervalMs(), config_.driftFadePct());
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedNameChange:
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedPasswordChange:
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedLedStrip:
      applyLedConfig();
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::AppliedBrightnessChange:
      paint_.setBrightness(config_.brightness());
      status_.triggerOnChange();
      break;
    case wisp::DispatchResult::PollStatus:
      status_.requestEmit();
      break;
    case wisp::DispatchResult::PollConfig:
      status_.pollConfig();
      break;
    case wisp::DispatchResult::Rejected:
    case wisp::DispatchResult::Ignored:
    case wisp::DispatchResult::Malformed:
      break;
  }
}

void WispController::onOscPalette(const float* rgb, size_t colours,
                                  const char* via, uint32_t nowMs) {
  if (config_.sourceMode() != wisp::WispSourceMode::Osc) return;
  if (colours == 0) return;
  const size_t floats = colours * 3;
  if (isBlankPalette(rgb, floats)) {
    if (oscFresh_.live()) {
      holdOscStream();
      Serial.printf("[osc] blank palette via %s — holding lamps at Off\n",
                    via);
    }
    return;
  }
  const bool resumed = oscFresh_.onPalette(nowMs);
  const OscRole role = config_.oscRole();
  if (!resumed && role == oscLastRole_ && floats == oscLastFloats_ &&
      std::memcmp(rgb, oscLast_, floats * sizeof(float)) == 0) {
    return;
  }
  std::memcpy(oscLast_, rgb, floats * sizeof(float));
  oscLastFloats_ = floats;
  oscLastRole_ = role;

  oscScratch_.colors.resize(colours);
  for (size_t i = 0; i < colours; ++i) {
    PaletteColor& pc = oscScratch_.colors[i];
    pc = PaletteColor{};
    pc.r = rgb[i * 3 + 0];
    pc.g = rgb[i * 3 + 1];
    pc.b = rgb[i * 3 + 2];
  }
  // 8 hex chars fits SSO, so the id write never allocates.
  char id[9];
  std::snprintf(id, sizeof(id), "%08x",
                (unsigned)wisp::fnv1a(reinterpret_cast<const uint8_t*>(rgb),
                                      floats * sizeof(float)));
  oscScratch_.id = id;
  palette_.update(oscScratch_, nowMs);
  // The ring renders first because it re-opens the ArtNet gate on a resume;
  // the emit below would otherwise be dropped.
  renderRing();
  if (resumed) {
    oscPaintGate_.reset();
    oscPaintGate_.request(nowMs);
    paint_.setSourceLive(true);
    artnet_.onPaletteChanged();
    status_.requestEmit();
  } else if (oscPaintGate_.request(nowMs)) {
    pushOscPaint();
  }

  if (resumed || nowMs - oscLastLogMs_ >= 1000) {
    oscLastLogMs_ = nowMs;
    Serial.printf("[osc] palette n=%u via %s\n", (unsigned)colours, via);
  }
}

void WispController::onOscFeed(uint32_t nowMs) {
  if (config_.sourceMode() != wisp::WispSourceMode::Osc) return;
  oscFresh_.onFeed(nowMs);
}

void WispController::tickOscLiveness(uint32_t now) {
  if (config_.sourceMode() != wisp::WispSourceMode::Osc) return;
  if (oscFresh_.tick(now)) {
    holdOscStream();
    Serial.println(
        "[osc] palette stale — holding lamps at Off, paint mode kept");
    return;
  }
  if (oscFresh_.live() && oscPaintGate_.due(now)) pushOscPaint();
}

}  // namespace wisp
