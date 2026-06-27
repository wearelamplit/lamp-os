// WispConfig — thin NVS wrapper for wisp-side persistent settings.
//
// Persistent wisp state: the Flutter app pane (a) picks which Aurora zone
// this wisp follows, and (b) pushes WiFi credentials over BLE. Both ride
// in MSG_CONTROL_OP payloads (JSON), are dispatched by WispOpDispatcher,
// and land here for persistence.
//
// Storage: Arduino-ESP32 `Preferences` (NVS), namespace `"wisp"`. Keys are
// kept short because NVS imposes a 15-byte key length limit:
//   selZone   int32   selected Aurora zone, -1 = unset (0 is a valid zone)
//   wifiSsid  String  WiFi SSID for STA bring-up
//   wifiPw    String  WiFi password for STA bring-up
//
// The class caches the values in RAM after `begin()` so the read path doesn't
// hit NVS on every Aurora palette notification.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstdint>
#include <vector>

namespace wisp {

// Where the wisp gets its paint palette from. The mode controls which
// branch in main.cpp is allowed to feed CurrentPalette and whether the
// PaintDistributor is on or off.
//
//   Off    — no override broadcasts. The PaintDistributor is held off,
//            and a transition into Off triggers a one-shot RESTORE walk
//            so lamps drop any prior wisp-sourced override.
//   Manual — push the operator-defined manual palette into
//            CurrentPalette; Aurora's onActivePalette callback is
//            ignored while in this mode.
//   Aurora — Aurora subscription drives CurrentPalette as before.
//
// Wire encoding (NVS u8 + wispOp + wispStatus): 0=Off, 1=Manual, 2=Aurora.
// Default is Aurora to preserve legacy first-boot behavior (a fresh wisp
// follows Aurora's first-seen zone without operator intervention).
enum class WispSourceMode : uint8_t {
  Off    = 0,
  Manual = 1,
  Aurora = 2,
};

// Single color slot used by the manual palette. Plain RGB (no W) — the
// app picker emits W=0 for manual colors and the lamp grid handles the
// W channel locally based on its own headroom math.
struct ManualPaletteColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// Bound aligned with lamp_protocol::kMaxWispPaletteColors so the wisp's
// stored palette and the on-wire MSG_WISP_PALETTE broadcast share one
// ceiling. Aurora palettes can be larger than 50; setManualPalette truncates.
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

  // WiFi credential storage. Consumed by WifiLink (STA bring-up via
  // WiFi.mode(WIFI_STA) + WiFi.begin(ssid, pw)) — WispOpDispatcher
  // calls setWifi() on a setWifi op, then triggers wifiLink_->reconnect()
  // and stageBeacon_->refreshAdvert() so the new creds take effect
  // without a reboot.
  //
  // SECURITY: stored in plaintext NVS. ESP32 NVS is not encrypted by
  // default; anyone with flash-dump access can read these. Acceptable
  // for the current installation threat model; if we ever need to harden
  // this, options are: enable NVS encryption (esp_partition +
  // esp_secure_boot) or move credentials to a separate encrypted
  // partition.
  const String& wifiSsid() const { return wifiSsid_; }
  const String& wifiPw() const { return wifiPw_; }
  bool hasWifi() const { return wifiSsid_.length() > 0; }
  void setWifi(const String& ssid, const String& pw);
  void clearWifi();

  // Source mode — Off / Manual / Aurora. See enum doc above. Default
  // remains Aurora so a fresh wisp boots into the legacy first-seen-wins
  // behavior; persisted state survives reboots.
  WispSourceMode sourceMode() const { return sourceMode_; }
  void setSourceMode(WispSourceMode mode);

  // Operator-defined manual palette. Up to kManualPaletteMaxColors
  // colors; replace-only semantics (no per-color edit, matches the
  // Manual editor's "Save" gating in the app UI). Empty palette is
  // valid: while in Manual mode and empty, the wisp simply emits no
  // palette (lamps stay on their own behavior). NVS-persisted as a
  // packed RGB byte blob.
  const std::vector<ManualPaletteColor>& manualPalette() const {
    return manualPalette_;
  }
  void setManualPalette(const std::vector<ManualPaletteColor>& colors);

  // Snapshot the manual palette as packed RGB into the caller's buffer
  // (needs maxColors*3 bytes). Returns the color count written. Lock-guarded:
  // the only manualPalette accessor safe to call off the loop task (the
  // StatusBeacon timer-service emit path).
  size_t copyManualPalette(uint8_t* outRgb, size_t maxColors) const;

  // Off-mode color. When sourceMode is Off, the wisp does NOT broadcast
  // a palette to the lamp grid (PaintDistributor stays held off) — but
  // it still has its own 30-pixel ring to drive. This color is what
  // that ring shows in Off. Defaults to a warm-white candle-amber tint
  // matching the pre-existing fallback so a fresh wisp boots
  // identically. Persisted as 3 NVS bytes.
  ManualPaletteColor offColor() const { return offColor_; }
  void setOffColor(ManualPaletteColor c);

 private:
  // Mutex handle — opaque to keep FreeRTOS out of the header. Cast
  // back to SemaphoreHandle_t in the .cpp. Same pattern as WispRoster.
  void* mutex_ = nullptr;

  Preferences prefs_;
  bool        opened_ = false;

  int    selectedZone_ = -1;
  String wifiSsid_;
  String wifiPw_;
  WispSourceMode sourceMode_ = WispSourceMode::Off;
  std::vector<ManualPaletteColor> manualPalette_;
  ManualPaletteColor offColor_ = {255, 150, 50};
};

}  // namespace wisp
