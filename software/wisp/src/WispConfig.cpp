#include "WispConfig.h"

#include <algorithm>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
// Native test build — stub the FreeRTOS surface to no-ops. Single-thread
// test harness has no concurrent access; the mutex acts solely as a
// sequence point on hardware, so dropping it for native is safe.
#include <cstddef>
typedef void* SemaphoreHandle_t;
#define pdTRUE         1
#define portMAX_DELAY  0xFFFFFFFFu
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  return reinterpret_cast<SemaphoreHandle_t>(0x1);
}
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
#endif

namespace wisp {

namespace {
inline SemaphoreHandle_t asHandle(void* m) {
  return reinterpret_cast<SemaphoreHandle_t>(m);
}

constexpr const char* kNamespace      = "wisp";
constexpr const char* kKeyZone        = "selZone";
constexpr const char* kKeySsid        = "wifiSsid";
constexpr const char* kKeyPw          = "wifiPw";
// Source-mode persistence. Single u8: 0=Off, 1=Manual, 2=Aurora.
// Stored as int to match the Preferences API; the runtime cast clamps
// out-of-range values to Aurora.
constexpr const char* kKeySourceMode  = "srcMode";
// Manual palette persistence. Packed RGB bytes — 3 bytes per
// color, up to kManualPaletteMaxColors. Stored as a blob so the count is
// implicit in the byte length (length % 3 == 0). Empty blob → empty
// palette.
constexpr const char* kKeyManualPalette = "manualPal";
// Off-mode color — three bytes (R, G, B). Stored as a fixed-3-byte blob
// rather than three individual int slots so the wire format / NVS
// footprint is identical to one ManualPaletteColor.
constexpr const char* kKeyOffColor      = "offColor";

WispSourceMode coerceSourceMode(int raw) {
  switch (raw) {
    case static_cast<int>(WispSourceMode::Off):
    case static_cast<int>(WispSourceMode::Manual):
    case static_cast<int>(WispSourceMode::Aurora):
      return static_cast<WispSourceMode>(raw);
    default:
      // Any unknown / corrupted persisted value falls back to Aurora so
      // a partially-flashed wisp doesn't drop into a stranded "Off" mode
      // with no operator nearby to fix it.
      return WispSourceMode::Aurora;
  }
}
}  // namespace

WispConfig::WispConfig()  { mutex_ = xSemaphoreCreateMutex(); }
WispConfig::~WispConfig() { if (mutex_) vSemaphoreDelete(asHandle(mutex_)); }

void WispConfig::begin() {
  if (opened_) return;
  // Preferences::begin(name, readonly=false). The Arduino-ESP32 API auto-
  // creates the namespace on first write, but opening RW ensures subsequent
  // sets won't have to reopen.
  opened_ = prefs_.begin(kNamespace, /*readOnly=*/false);
  if (!opened_) {
    Serial.println("[wisp.cfg] Preferences::begin('wisp') failed");
    selectedZone_ = -1;
    wifiSsid_     = String();
    wifiPw_       = String();
    return;
  }
  // getInt second arg is the default returned when key is missing.
  selectedZone_ = prefs_.getInt(kKeyZone, -1);
  wifiSsid_     = prefs_.getString(kKeySsid, String());
  wifiPw_       = prefs_.getString(kKeyPw, String());
  // Default to Off when the key is missing. The pre-Phase-E behaviour
  // was Aurora-for-everyone, but that pretended an event was in progress
  // on a fresh wisp with no Aurora bus on the network, which (combined
  // with the empty-palette zero-broadcast) fought every BLE base-colour
  // edit the operator made. A fresh wisp staying quiet until the
  // operator picks Manual + a palette OR explicitly flips Aurora on for
  // a show is the right default. Fielded wisps with NVS-saved values
  // are unaffected.
  sourceMode_   = coerceSourceMode(
      prefs_.getInt(kKeySourceMode,
                    static_cast<int>(WispSourceMode::Off)));

  // Manual palette: bytes blob, 3 bytes per color, capped at the
  // protocol max. getBytesLength returns 0 for missing keys.
  manualPalette_.clear();
  const size_t paletteLen = prefs_.getBytesLength(kKeyManualPalette);
  if (paletteLen > 0 && paletteLen % 3 == 0) {
    const size_t colorCount =
        std::min<size_t>(paletteLen / 3, kManualPaletteMaxColors);
    uint8_t buf[kManualPaletteMaxColors * 3];
    const size_t toRead = colorCount * 3;
    const size_t got = prefs_.getBytes(kKeyManualPalette, buf, toRead);
    if (got == toRead) {
      manualPalette_.reserve(colorCount);
      for (size_t i = 0; i < colorCount; ++i) {
        ManualPaletteColor c;
        c.r = buf[i * 3 + 0];
        c.g = buf[i * 3 + 1];
        c.b = buf[i * 3 + 2];
        manualPalette_.push_back(c);
      }
    }
  } else if (paletteLen > 0) {
    Serial.printf("[wisp.cfg] manualPalette blob has odd length %u; ignoring\n",
                  (unsigned)paletteLen);
  }

  // Off-mode color: 3 bytes. Missing or wrong-sized blob falls back to the
  // pre-existing warm-white default already set in the field initialiser.
  const size_t offColorLen = prefs_.getBytesLength(kKeyOffColor);
  if (offColorLen == 3) {
    uint8_t buf[3];
    if (prefs_.getBytes(kKeyOffColor, buf, 3) == 3) {
      offColor_.r = buf[0];
      offColor_.g = buf[1];
      offColor_.b = buf[2];
    }
  }

  Serial.printf("[wisp.cfg] loaded selZone=%d ssid='%s' pw=%s "
                "srcMode=%d manualColors=%u offColor=%u,%u,%u\n",
                selectedZone_, wifiSsid_.c_str(),
                wifiPw_.length() ? "<set>" : "<empty>",
                static_cast<int>(sourceMode_),
                (unsigned)manualPalette_.size(),
                offColor_.r, offColor_.g, offColor_.b);
}

void WispConfig::setSelectedZone(int zone) {
  if (zone < 0) {
    Serial.printf("[wisp.cfg] setSelectedZone(%d) rejected — use clearSelectedZone()\n",
                  zone);
    return;
  }
  selectedZone_ = zone;
  if (opened_) {
    prefs_.putInt(kKeyZone, zone);
  }
  Serial.printf("[wisp.cfg] selZone <= %d\n", zone);
}

void WispConfig::clearSelectedZone() {
  selectedZone_ = -1;
  if (opened_) {
    prefs_.remove(kKeyZone);
  }
  Serial.println("[wisp.cfg] selZone cleared");
}

void WispConfig::setWifi(const String& ssid, const String& pw) {
  wifiSsid_ = ssid;
  wifiPw_   = pw;
  if (opened_) {
    prefs_.putString(kKeySsid, ssid);
    prefs_.putString(kKeyPw, pw);
  }
  Serial.printf("[wisp.cfg] wifi <= ssid='%s' pw=<%u chars>\n",
                ssid.c_str(), (unsigned)pw.length());
}

void WispConfig::clearWifi() {
  wifiSsid_ = String();
  wifiPw_   = String();
  if (opened_) {
    prefs_.remove(kKeySsid);
    prefs_.remove(kKeyPw);
  }
  Serial.println("[wisp.cfg] wifi cleared");
}

void WispConfig::setSourceMode(WispSourceMode mode) {
  sourceMode_ = mode;
  if (opened_) {
    prefs_.putInt(kKeySourceMode, static_cast<int>(mode));
  }
  Serial.printf("[wisp.cfg] sourceMode <= %d\n", static_cast<int>(mode));
}

void WispConfig::setManualPalette(
    const std::vector<ManualPaletteColor>& colors) {
  // Cap at the protocol max so the persisted blob never grows past the
  // budgeted size — keeps the wispStatus JSON within CONTROL_MAX_PAYLOAD
  // regardless of what the app pushed.
  const size_t n = std::min<size_t>(colors.size(), kManualPaletteMaxColors);
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  manualPalette_.assign(colors.begin(), colors.begin() + n);
  xSemaphoreGive(asHandle(mutex_));
  if (opened_) {
    if (n == 0) {
      prefs_.remove(kKeyManualPalette);
    } else {
      uint8_t buf[kManualPaletteMaxColors * 3];
      for (size_t i = 0; i < n; ++i) {
        buf[i * 3 + 0] = manualPalette_[i].r;
        buf[i * 3 + 1] = manualPalette_[i].g;
        buf[i * 3 + 2] = manualPalette_[i].b;
      }
      prefs_.putBytes(kKeyManualPalette, buf, n * 3);
    }
  }
  Serial.printf("[wisp.cfg] manualPalette <= %u colors\n", (unsigned)n);
}

void WispConfig::setOffColor(ManualPaletteColor c) {
  offColor_ = c;
  if (opened_) {
    const uint8_t buf[3] = {c.r, c.g, c.b};
    prefs_.putBytes(kKeyOffColor, buf, 3);
  }
  Serial.printf("[wisp.cfg] offColor <= %u,%u,%u\n", c.r, c.g, c.b);
}

size_t WispConfig::copyManualPalette(uint8_t* out, size_t maxColors) const {
  if (!out || !maxColors) return 0;
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  size_t n = std::min(manualPalette_.size(), maxColors);
  for (size_t i = 0; i < n; ++i) {
    out[i * 3 + 0] = manualPalette_[i].r;
    out[i * 3 + 1] = manualPalette_[i].g;
    out[i * 3 + 2] = manualPalette_[i].b;
  }
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

}  // namespace wisp
