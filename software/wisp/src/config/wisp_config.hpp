// WispConfig is NVS-backed persistent settings, namespace "wisp".
// Values are cached in RAM after begin(); setters write-through to NVS.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstdint>
#include <vector>

#include <lampos/led_types.hpp>

#include "config/manual_palette_color.hpp"
#include "osc/osc_role.hpp"

namespace wisp {

// Wire encoding (NVS u8 + wispOp + wispStatus): 0=Off, 1=Manual, 3=Osc.
// 2 is retired; a stored 2 loads as Off.
enum class WispSourceMode : uint8_t {
  Off    = 0,
  Manual = 1,
  Osc    = 3,
};

// Must not exceed lamp_protocol::kMaxWispPaletteColors; setManualPalette enforces this.
inline constexpr size_t kManualPaletteMaxColors = 50;

class WispConfig {
 public:
  WispConfig();
  ~WispConfig();

  // Open the `wisp` Preferences namespace in RW mode and cache the values.
  // Safe to call once at boot from setup().
  void begin();

  // SECURITY: stored in plaintext NVS (not encrypted by default). Anyone with
  // flash-dump access can read these. Harden via NVS encryption if needed.
  const String& wifiSsid() const { return wifiSsid_; }
  const String& wifiPw() const { return wifiPw_; }
  bool hasWifi() const { return wifiSsid_.length() > 0; }
  void setWifi(const String& ssid, const String& pw);
  void clearWifi();

  WispSourceMode sourceMode() const { return sourceMode_; }
  void setSourceMode(WispSourceMode mode);

  // Straylight palette role followed in Osc mode.
  OscRole oscRole() const { return oscRole_; }
  void setOscRole(OscRole role);

  // Replace-only; empty is valid (lamps revert to their own behavior).
  const std::vector<ManualPaletteColor>& manualPalette() const {
    return manualPalette_;
  }
  void setManualPalette(const std::vector<ManualPaletteColor>& colors);

  // Fill `outRgb` (maxColors * 3) and `outW` (maxColors) planes; matches
  // the MSG_WISP_PALETTE wire layout. Lock-guarded: safe to call from the
  // StatusEmitter timer-service task.
  size_t copyManualPalette(uint8_t* outRgb, uint8_t* outW,
                           size_t maxColors) const;

  // Ring color in Off mode (PaintDistributor stays idle). Persisted as 4 NVS bytes.
  ManualPaletteColor offColor() const { return offColor_; }
  void setOffColor(ManualPaletteColor c);

  // Bumping re-rolls per-lamp color assignments fleet-wide.
  uint8_t shuffleSeed() const { return shuffleSeed_; }
  void bumpShuffleSeed();

  // Color drift interval (ms), and the Fade setting (0..100) that sets each
  // drift step's fade length (see driftFadeMs).
  uint32_t driftIntervalMs() const { return driftIntervalMs_; }
  uint8_t driftFadePct() const { return driftFadePct_; }
  void setDrift(uint32_t intervalMs, uint8_t fadePct);

  // Display name shown in wispStatus. Clamped to ≤20 chars on write.
  const String& name() const { return name_; }
  void setName(const String& name);

  // Control password for sealing wispOps. Empty = open access.
  const String& password() const { return password_; }
  void setPassword(const String& pw);

  lampos::led::ByteOrder ledFormat() const { return ledFormat_; }
  uint16_t pixelCount() const { return pixelCount_; }
  void setLedFormat(lampos::led::ByteOrder b);
  void setPixelCount(uint16_t n);

  // Space-brightness factor (0..100) the wisp asserts on its claimed lamps.
  // 100 = untouched; the lamp applies it as a floored multiplier. Setter
  // clamps to 100.
  uint8_t brightness() const { return brightness_; }
  void setBrightness(uint8_t pct);

  // Monotonic count of accepted+applied sealed wispOps. RAM-only (a reboot
  // resets it); rides wispStatus so the app can confirm a sealed op landed.
  uint32_t opSeq() const { return opSeq_; }
  void bumpOpSeq() { ++opSeq_; }

 private:
  // Opaque; keeps FreeRTOS out of the header.
  void* mutex_ = nullptr;

  Preferences prefs_;
  bool        opened_ = false;

  String wifiSsid_;
  String wifiPw_;
  WispSourceMode sourceMode_ = WispSourceMode::Off;
  OscRole oscRole_ = OscRole::Primary;
  std::vector<ManualPaletteColor> manualPalette_;
  ManualPaletteColor offColor_ = {255, 150, 50, 0};
  uint8_t shuffleSeed_ = 0;
  uint32_t driftIntervalMs_ = 120000;
  uint8_t driftFadePct_ = 50;
  String name_;
  String password_;
  lampos::led::ByteOrder ledFormat_ = lampos::led::ByteOrder::GRB;
  uint16_t pixelCount_ = 30;
  uint8_t brightness_ = 100;
  uint32_t opSeq_ = 0;
};

}  // namespace wisp
