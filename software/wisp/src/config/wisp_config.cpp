#include "config/wisp_config.hpp"

#include <algorithm>

#include "config/zone_selector.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
// Native test build — no-op FreeRTOS stubs. Single-threaded; mutex is
// a sequence point on hardware only, safe to drop here.
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
constexpr const char* kKeySourceMode  = "srcMode";
// Packed RGB bytes: count implicit in blob length (length % 3 == 0).
constexpr const char* kKeyManualPalette = "manualPal";
constexpr const char* kKeyOffColor      = "offColor";
constexpr const char* kKeyShuffleSeed   = "shufSeed";
constexpr const char* kKeyDriftIntv     = "driftIntv";
constexpr const char* kKeyDriftFade     = "driftFade";

WispSourceMode coerceSourceMode(int raw) {
  switch (raw) {
    case static_cast<int>(WispSourceMode::Off):
    case static_cast<int>(WispSourceMode::Manual):
    case static_cast<int>(WispSourceMode::Aurora):
      return static_cast<WispSourceMode>(raw);
    default:
      // Unknown / corrupted NVS value: fall back to Aurora so a partially-
      // flashed wisp doesn't strand itself in Off with no operator nearby.
      return WispSourceMode::Aurora;
  }
}
}  // namespace

WispConfig::WispConfig()  { mutex_ = xSemaphoreCreateMutex(); }
WispConfig::~WispConfig() { if (mutex_) vSemaphoreDelete(asHandle(mutex_)); }

void WispConfig::begin() {
  if (opened_) return;
  opened_ = prefs_.begin(kNamespace, /*readOnly=*/false);
  if (!opened_) {
    Serial.println("[wisp.cfg] Preferences::begin('wisp') failed");
    selectedZone_ = -1;
    wifiSsid_     = String();
    wifiPw_       = String();
    return;
  }
  selectedZone_ = prefs_.getInt(kKeyZone, -1);
  wifiSsid_     = prefs_.getString(kKeySsid, String());
  wifiPw_       = prefs_.getString(kKeyPw, String());
  // Off default: a fresh wisp with no Aurora bus otherwise broadcasts
  // empty-palette frames that fight operator BLE color edits.
  sourceMode_   = coerceSourceMode(
      prefs_.getInt(kKeySourceMode,
                    static_cast<int>(WispSourceMode::Off)));

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

  const size_t offColorLen = prefs_.getBytesLength(kKeyOffColor);
  if (offColorLen == 3) {
    uint8_t buf[3];
    if (prefs_.getBytes(kKeyOffColor, buf, 3) == 3) {
      offColor_.r = buf[0];
      offColor_.g = buf[1];
      offColor_.b = buf[2];
    }
  }

  shuffleSeed_ = static_cast<uint8_t>(prefs_.getInt(kKeyShuffleSeed, 0) & 0xFF);

  driftIntervalMs_ = prefs_.getUInt(kKeyDriftIntv, 120000);
  driftFadePct_ = static_cast<uint8_t>(prefs_.getUChar(kKeyDriftFade, 50));

  Serial.printf("[wisp.cfg] loaded selZone=%d ssid='%s' pw=%s "
                "srcMode=%d manualColors=%u offColor=%u,%u,%u shuffleSeed=%u "
                "driftIntv=%u ms driftFade=%u%%\n",
                selectedZone_, wifiSsid_.c_str(),
                wifiPw_.length() ? "<set>" : "<empty>",
                static_cast<int>(sourceMode_),
                (unsigned)manualPalette_.size(),
                offColor_.r, offColor_.g, offColor_.b,
                (unsigned)shuffleSeed_,
                (unsigned)driftIntervalMs_,
                (unsigned)driftFadePct_);
}

void WispConfig::setSelectedZone(int zone) {
  if (!isValidZone(zone)) {
    Serial.printf("[wisp.cfg] setSelectedZone(%d) rejected — use clearSelectedZone() for <0, or zone must be 0..%d\n",
                  zone, kMaxZoneId);
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

void WispConfig::bumpShuffleSeed() {
  shuffleSeed_ = (shuffleSeed_ + 1) & 0xFF;
  if (opened_) {
    prefs_.putInt(kKeyShuffleSeed, shuffleSeed_);
  }
  Serial.printf("[wisp.cfg] shuffleSeed <= %u\n", (unsigned)shuffleSeed_);
}

void WispConfig::setDrift(uint32_t intervalMs, uint8_t fadePct) {
  driftIntervalMs_ = intervalMs;
  driftFadePct_ = fadePct;
  if (opened_) {
    prefs_.putUInt(kKeyDriftIntv, intervalMs);
    prefs_.putUChar(kKeyDriftFade, fadePct);
  }
  Serial.printf("[wisp.cfg] drift <= %lu ms, %u%% fade\n", intervalMs, fadePct);
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
