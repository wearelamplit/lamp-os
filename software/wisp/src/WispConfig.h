// WispConfig — thin NVS wrapper for wisp-side persistent settings (zone +
// WiFi creds, pushed by the app pane via MSG_CONTROL_OP and dispatched by
// WispOpDispatcher).
//
// Storage: Preferences (NVS), namespace "wisp". Keys kept short (NVS caps key
// length at 15 bytes):
//   selZone   int32   selected Aurora zone, -1 = unset (0 is valid)
//   wifiSsid  String  WiFi SSID for STA bring-up
//   wifiPw    String  WiFi password for STA bring-up
// Values cache in RAM after begin() so the read path skips NVS on every
// Aurora notification.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <cstdint>
#include <vector>

namespace wisp {

// Where the wisp gets its paint palette. Controls which branch in main.cpp
// feeds CurrentPalette and whether PaintDistributor runs.
//   Off    no override broadcasts; PaintDistributor held off, and a
//          transition into Off triggers a one-shot RESTORE walk.
//   Manual push the operator's manual palette into CurrentPalette; Aurora
//          callbacks are ignored.
//   Aurora Aurora subscription drives CurrentPalette.
// Wire encoding (NVS u8 + wispOp + wispStatus): 0=Off, 1=Manual, 2=Aurora.
enum class WispSourceMode : uint8_t {
  Off    = 0,
  Manual = 1,
  Aurora = 2,
};

// Single manual-palette color slot. Plain RGB (no W); the app picker emits
// W=0 and the lamp grid derives W locally from its own headroom math.
struct ManualPaletteColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// Aligned with lamp_protocol::kMaxWispPaletteColors so the stored palette and
// the MSG_WISP_PALETTE broadcast share one ceiling. setManualPalette
// truncates larger Aurora palettes (emit-side logs once on oversize).
inline constexpr size_t kManualPaletteMaxColors = 50;

class WispConfig {
 public:
  // Open the `wisp` Preferences namespace in RW mode and cache the values.
  // Safe to call once at boot from setup().
  void begin();

  // -1 = no zone selected. 0 is a valid Aurora zone, so 0 can't be the
  // unset sentinel.
  int selectedZone() const { return selectedZone_; }
  bool hasSelectedZone() const { return selectedZone_ >= 0; }

  // Write-through to NVS. Negative values are rejected (use clearSelectedZone).
  void setSelectedZone(int zone);

  // Removes the key from NVS and resets the cache to -1.
  void clearSelectedZone();

  // WiFi creds consumed by WifiLink (STA bring-up). WispOpDispatcher calls
  // setWifi() then kicks reconnect() + refreshAdvert() so new creds apply
  // without a reboot.
  //
  // Stored in plaintext NVS (not encrypted by default; a flash dump reveals
  // them). Acceptable for the current threat model; hardening options are NVS
  // encryption or a separate encrypted partition.
  const String& wifiSsid() const { return wifiSsid_; }
  const String& wifiPw() const { return wifiPw_; }
  bool hasWifi() const { return wifiSsid_.length() > 0; }
  void setWifi(const String& ssid, const String& pw);
  void clearWifi();

  // Source mode (Off/Manual/Aurora). See enum doc above; persisted across
  // reboots.
  WispSourceMode sourceMode() const { return sourceMode_; }
  void setSourceMode(WispSourceMode mode);

  // Operator manual palette, up to kManualPaletteMaxColors, replace-only.
  // Empty is valid (Manual + empty emits no palette; lamps keep their own
  // behavior). NVS-persisted as a packed RGB blob.
  const std::vector<ManualPaletteColor>& manualPalette() const {
    return manualPalette_;
  }
  void setManualPalette(const std::vector<ManualPaletteColor>& colors);

  // Off-mode ring color. In Off the wisp broadcasts no palette but still
  // drives its 30-pixel ring; this is what the ring shows. Defaults to a
  // warm candle-amber. Persisted as 3 NVS bytes.
  ManualPaletteColor offColor() const { return offColor_; }
  void setOffColor(ManualPaletteColor c);

 private:
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
