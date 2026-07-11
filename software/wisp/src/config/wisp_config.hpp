// WispConfig — NVS-backed persistent settings, namespace "wisp".
// Values are cached in RAM after begin(); setters write-through to NVS.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstdint>
#include <vector>

namespace wisp {

// Wire encoding (NVS u8 + wispOp + wispStatus): 0=Off, 1=Manual, 2=Aurora.
enum class WispSourceMode : uint8_t {
  Off    = 0,
  Manual = 1,
  Aurora = 2,
};

// Plain RGB; the lamp grid handles W locally.
struct ManualPaletteColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// Aligned with lamp_protocol::kMaxWispPaletteColors; setManualPalette truncates.
inline constexpr size_t kManualPaletteMaxColors = 50;

class WispConfig {
 public:
  WispConfig();
  ~WispConfig();

  // Open the `wisp` Preferences namespace in RW mode and cache the values.
  // Safe to call once at boot from setup().
  void begin();

  // -1 sentinel means "no zone selected yet". 0 is a valid Aurora zone, so we
  // can't use 0 as the unset sentinel.
  int selectedZone() const { return selectedZone_; }
  bool hasSelectedZone() const { return selectedZone_ >= 0; }

  // Write-through to NVS. Negative values are rejected (use clearSelectedZone).
  void setSelectedZone(int zone);

  // Removes the key from NVS and resets the cache to -1.
  void clearSelectedZone();

  // SECURITY: stored in plaintext NVS (not encrypted by default). Anyone with
  // flash-dump access can read these. Harden via NVS encryption if needed.
  const String& wifiSsid() const { return wifiSsid_; }
  const String& wifiPw() const { return wifiPw_; }
  bool hasWifi() const { return wifiSsid_.length() > 0; }
  void setWifi(const String& ssid, const String& pw);
  void clearWifi();

  WispSourceMode sourceMode() const { return sourceMode_; }
  void setSourceMode(WispSourceMode mode);

  // Replace-only; empty is valid (lamps revert to their own behavior).
  const std::vector<ManualPaletteColor>& manualPalette() const {
    return manualPalette_;
  }
  void setManualPalette(const std::vector<ManualPaletteColor>& colors);

  // Lock-guarded: safe to call from the StatusEmitter timer-service task.
  size_t copyManualPalette(uint8_t* outRgb, size_t maxColors) const;

  // Ring color in Off mode (PaintDistributor stays idle). Persisted as 3 NVS bytes.
  ManualPaletteColor offColor() const { return offColor_; }
  void setOffColor(ManualPaletteColor c);

  // Bumping re-rolls per-lamp color assignments fleet-wide (XORed into TupleSampler salts).
  uint8_t shuffleSeed() const { return shuffleSeed_; }
  void bumpShuffleSeed();

  // Color drift interval (ms) and fade strength (0..100%).
  uint32_t driftIntervalMs() const { return driftIntervalMs_; }
  uint8_t driftFadePct() const { return driftFadePct_; }
  void setDrift(uint32_t intervalMs, uint8_t fadePct);

  // Display name shown in wispStatus. Clamped to ≤20 chars on write.
  const String& name() const { return name_; }
  void setName(const String& name);

  // Control password for sealing wispOps. Empty = open access.
  const String& password() const { return password_; }
  void setPassword(const String& pw);

 private:
  // Opaque to keep FreeRTOS out of the header; cast to SemaphoreHandle_t in .cpp.
  void* mutex_ = nullptr;

  Preferences prefs_;
  bool        opened_ = false;

  int    selectedZone_ = -1;
  String wifiSsid_;
  String wifiPw_;
  WispSourceMode sourceMode_ = WispSourceMode::Off;
  std::vector<ManualPaletteColor> manualPalette_;
  ManualPaletteColor offColor_ = {255, 150, 50};
  uint8_t shuffleSeed_ = 0;
  uint32_t driftIntervalMs_ = 120000;
  uint8_t driftFadePct_ = 50;
  String name_;
  String password_;
};

}  // namespace wisp
