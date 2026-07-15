#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "util/color.hpp"

namespace lamp {

// Class-default colors for an unconfigured lamp (empty or corrupt NVS). Both
// are low-power: a purple base stem, and a warm white driven by the shade's
// dedicated W channel rather than full RGB. applyDefaults() overwrites a
// single-entry vector still holding one of these with the variant's injected
// color, so a configured lamp never matches.
constexpr Color kBaseDefaultColor(0x30, 0x07, 0x83, 0x00);
constexpr Color kShadeDefaultColor(0x00, 0x00, 0x00, 0xFF);

/**
 * @brief Social personality mode — tunes how often + how eagerly the lamp
 *        greets nearby peers via SocialBehavior. Stored as uint8_t for
 *        wire/NVS compatibility. Default Ambivert.
 */
enum class SocialMode : uint8_t {
  Introvert = 0,
  Ambivert = 1,
  Extrovert = 2,
};

/**
 * @brief Global lamp settings to control initialization
 * @property name - a name that can be used to identify this lamp. BLE device name is clipped to 12 characters on the wire
 * @property brightness - global brightness level for the lamp as a percentage
 * @property password - password to protect lamp BLE control surface
 * @property advancedEnabled - if true, advanced settings UI is unlocked
 * @property socialMode - personality flavor: introvert / ambivert (default) / extrovert
 * @property lampType - variant identity string (e.g. "standard", "snafu"). Firmware-owned.
 */
class LampSettings {
 public:
  std::string name = "stray";
  uint8_t brightness = 100;
  std::string password = "";
  // True once the lamp has been claimed/configured via the app. Drives the
  // adopt-vs-open routing (advertised as a capability bit) so the check no
  // longer relies on matching default name/colors. Default false → a fresh
  // or custom lamp is "unconfigured" and gets the onboarding wizard.
  bool setup = false;
  bool advancedEnabled = false;
  // Default true so existing NVS payloads without the field opt in.
  bool webappEnabled = true;
  SocialMode socialMode = SocialMode::Ambivert;
  // Variant identity. Firmware-owned; loaded from NVS at boot.
  // Emitted in asLampJson for app display; settings_blob inbound writes
  // IGNORE this field (firmware authoritative).
  std::string lampType;  // empty string on first boot before any resolution
};

/**
 * @brief One physical strip run within a role: its app-editable palette + pixel
 *        count. Geometry (driver/pin/offset) lives in the variant's StripSpec;
 *        this is the persisted, editable half.
 * @property px - 0 = unset; applyDefaults fills from the variant default. See resolveConfiguredPx.
 */
struct SegmentSettings {
  std::string name;
  uint8_t px = 0;
  std::vector<Color> colors;
};

// Role pixel-space size. ≤255 by construction (config_codec::fromJson clamps
// the sum before boot buffer-sizing).
inline uint8_t segmentsSumPx(const std::vector<SegmentSettings>& segments) {
  unsigned total = 0;
  for (const auto& s : segments) total += s.px;
  return total > 255 ? 255 : static_cast<uint8_t>(total);
}

/**
 * @brief Shade role: an ordered list of named segments.
 */
class ShadeSettings {
 public:
  std::vector<SegmentSettings> segments = {{"Shade", 0, {kShadeDefaultColor}}};
  bool colorsEditable = true;  // Emitted in asShadeJson; app hides color picker when false.

  uint8_t sumPx() const { return segmentsSumPx(segments); }
  // Representative palette (HELLO/greet/adv): the broadcast segment, first.
  std::vector<Color>& broadcastColors() { return segments.front().colors; }
  const std::vector<Color>& broadcastColors() const { return segments.front().colors; }
};

/**
 * @brief Base role: an ordered list of named segments, plus base-only knockout
 *        profile + active-color index.
 * @property knockoutPixels - per-pixel brightness profile across the whole role
 * @property ac - the preferred color index in the broadcast segment's gradient
 */
class BaseSettings {
 public:
  std::vector<SegmentSettings> segments = {{"Base", 0, {kBaseDefaultColor}}};
  std::vector<uint8_t> knockoutPixels = std::vector<uint8_t>(50, (uint8_t)100);
  uint8_t ac = 0;
  bool colorsEditable = true;  // Emitted in asBaseJson; app hides color picker when false.

  uint8_t sumPx() const { return segmentsSumPx(segments); }
  std::vector<Color>& broadcastColors() { return segments.front().colors; }
  const std::vector<Color>& broadcastColors() const { return segments.front().colors; }
};

// Resolve a loaded pixel count against a variant default. A persisted px
// of 0 means the field was absent from NVS (the loader parses a missing
// "px" key to 0), i.e. a fresh lamp — fill from the variant default. Any
// real stored value wins, including one that equals a former factory
// baseline (32/38 shade, 35/36 base) that the old applyDefaults guard
// wrongly clobbered. Callers pass a non-zero default (the variant px, or
// the struct class-default), so a configured lamp's choice is sacred.
inline uint8_t resolveConfiguredPx(uint8_t stored, uint8_t dflt) {
  return stored != 0 ? stored : dflt;
}

/**
 * @brief Configuration for a single expression with generic parameter system
 */
class ExpressionConfig {
 public:
  std::string type = "";           // Expression type (e.g., "glitchy", "shifty")
  bool enabled = false;            // Whether expression is active
  std::vector<Color> colors;       // Color palette for expression
  uint32_t intervalMin = 60;       // Min interval in seconds
  uint32_t intervalMax = 900;      // Max interval in seconds
  uint8_t target = 3;              // TARGET_SHADE=1, TARGET_BASE=2, TARGET_BOTH=3

  // Generic parameter storage for expression-specific values
  std::map<std::string, uint32_t> parameters;

  // Helper methods for parameter access
  uint32_t getParameter(const std::string& name, uint32_t defaultValue) const {
    auto it = parameters.find(name);
    if (it != parameters.end()) {
      return it->second;
    }
    return defaultValue;
  }

  void setParameter(const std::string& name, uint32_t value) {
    parameters[name] = value;
  }
};

/**
 * @brief Settings for lamp expressions
 */
class ExpressionSettings {
 public:
  std::vector<ExpressionConfig> expressions;
};

/**
 * @brief Home mode: presence-only detection of the user's home WiFi.
 *        The lamp NEVER associates to the AP — it just sniffs beacons
 *        and treats the SSID being visible as "I'm at home". No password
 *        is ever stored or transmitted (security: no MITM risk against
 *        a hostile AP broadcasting the same SSID).
 *
 *        While in home mode the lamp uses `brightness` (this struct's)
 *        instead of LampSettings.brightness, and the compositor pauses
 *        behaviors flagged allowedInHomeMode=false (e.g. SocialBehavior).
 *
 * @property ssid       the SSID to watch for. Set via wifi_op setHomeSsid
 *                      (which the app sends when the user taps an entry
 *                      from a scan list). Empty = no home network.
 * @property brightness brightness to use when home mode is active.
 * @property enabled    soft on/off. When false the lamp ignores SSID
 *                      visibility and stays in regular mode.
 */
class HomeModeSettings {
 public:
  std::string ssid;
  uint8_t brightness = 60;
  bool enabled = false;
};

}  // namespace lamp
