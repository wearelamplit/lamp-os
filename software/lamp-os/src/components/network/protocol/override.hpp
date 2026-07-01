#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"

// =============================================================================
// override.hpp — the transient override/restore family.
// Covers MSG_OVERRIDE_COLORS (0x21), MSG_RESTORE_COLORS (0x22),
//        MSG_OVERRIDE_BRIGHTNESS (0x23), MSG_RESTORE_BRIGHTNESS (0x24).
// build/parse: build{Override,Restore}{Colors,Brightness} /
//              parse{Override,Restore}{Colors,Brightness}.
// =============================================================================
//
// Shared prefix (all four messages), bytes 0..21:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    6    targetMac
//   18    1    surface (OverrideSurface: Base/Shade/BaseAndShade)
//   19    1    sourceKind (OverrideSource)
//   20    2    fadeDurationMs (LE)
//
// Per-message tail after byte 22:
//   OVERRIDE_COLORS   : [22]=numColors (1..8), then numColors*4 RGBW bytes.
//                       OVERRIDE_COLORS_FIXED_SIZE=23, MAX_SIZE=55.
//   OVERRIDE_BRIGHTNESS: [22]=brightness (kBrightnessOverrideMin..100).
//                       OVERRIDE_BRIGHTNESS_FIXED_SIZE=23.
//   RESTORE_COLORS / RESTORE_BRIGHTNESS: no tail. RESTORE_FIXED_SIZE=22.

namespace lamp_protocol {

// Single-source-of-truth caps.
constexpr size_t kMaxOverrideColorsPerFrame = 8;   // ESP-NOW 250-byte cap math
constexpr uint8_t kBrightnessOverrideMin    = 5;   // anti-defeat floor

// Surface byte values used by the override/restore family. `BaseAndShade`
// means numColors=2 carries a pair: colors[0] for base, colors[1] for
// shade — one frame, two surfaces, distinct colors. The wisp's paint
// distributor uses BaseAndShade to halve ESP-NOW frame count per peer
// per cycle (was Base+Shade as two separate frames).
enum class OverrideSurface : uint8_t {
  Base         = 0x01,
  Shade        = 0x02,
  BaseAndShade = 0x03,
};

// Discriminator for who originated an override. 0x10..0xFE is user-
// defined for forward compat; Any (0xFF) is an internal sentinel used
// by the watchdog and force-restore paths to bypass source-discriminated
// drop logic — never appears on the wire.
enum class OverrideSource : uint8_t {
  None     = 0x00,
  Wisp     = 0x01,
  Any      = 0xFF,
};

// MSG_OVERRIDE_COLORS fixed prefix:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2) + numColors(1)
// = 23 bytes; colors[numColors * 4] follow. Min numColors=1 → 27 total.
constexpr size_t OVERRIDE_COLORS_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2 + 1;  // 23
constexpr size_t OVERRIDE_COLORS_MAX_SIZE   = OVERRIDE_COLORS_FIXED_SIZE +
                                              kMaxOverrideColorsPerFrame * 4;       // 55

// MSG_RESTORE_COLORS / MSG_RESTORE_BRIGHTNESS:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2)
// = 22 bytes fixed (no payload tail).
constexpr size_t RESTORE_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2;  // 22

// MSG_OVERRIDE_BRIGHTNESS:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2) + brightness(1)
// = 23 bytes fixed.
constexpr size_t OVERRIDE_BRIGHTNESS_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2 + 1;  // 23

struct ParsedOverrideColors {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
  uint8_t         numColors;
  uint8_t         colors[kMaxOverrideColorsPerFrame][4];  // RGBW per entry
};

struct ParsedRestoreColors {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
};

struct ParsedOverrideBrightness {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
  uint8_t         brightness;   // 0..100, validated against kBrightnessOverrideMin..100
};

struct ParsedRestoreBrightness {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
};

namespace detail {

inline bool isValidOverrideSurfaceByte(uint8_t b) {
  return b == static_cast<uint8_t>(OverrideSurface::Base) ||
         b == static_cast<uint8_t>(OverrideSurface::Shade) ||
         b == static_cast<uint8_t>(OverrideSurface::BaseAndShade);
}

// 0x00 None, 0x01 Wisp, 0xFF Any, 0x10..0xFE user-defined.
// Reject 0x02..0x0F (reserved) explicitly.
inline bool isValidOverrideSourceByte(uint8_t b) {
  if (b == static_cast<uint8_t>(OverrideSource::None)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Wisp)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Any)) return true;
  if (b >= 0x10 && b <= 0xFE) return true;
  return false;  // 0x02..0x0F reserved
}

}  // namespace detail

// Build a MSG_OVERRIDE_COLORS frame. `numColors` must be 1..kMaxOverrideColorsPerFrame.
// `colorsRGBW` is numColors * 4 bytes. Returns total bytes written on success.
inline size_t buildOverrideColors(uint8_t* buf, size_t bufLen, uint16_t seq,
                                  const uint8_t sourceMac[6],
                                  const uint8_t targetMac[6],
                                  OverrideSurface surface,
                                  OverrideSource sourceKind,
                                  uint16_t fadeDurationMs,
                                  const uint8_t* colorsRGBW,
                                  uint8_t numColors) {
  if (!buf || !sourceMac || !targetMac || !colorsRGBW) return 0;
  if (numColors == 0 || numColors > kMaxOverrideColorsPerFrame) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  const size_t total = OVERRIDE_COLORS_FIXED_SIZE + numColors * 4u;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_OVERRIDE_COLORS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  buf[22] = numColors;
  std::memcpy(&buf[OVERRIDE_COLORS_FIXED_SIZE], colorsRGBW, numColors * 4u);
  return total;
}

// Build a MSG_RESTORE_COLORS frame. Returns RESTORE_FIXED_SIZE on success.
inline size_t buildRestoreColors(uint8_t* buf, size_t bufLen, uint16_t seq,
                                 const uint8_t sourceMac[6],
                                 const uint8_t targetMac[6],
                                 OverrideSurface surface,
                                 OverrideSource sourceKind,
                                 uint16_t fadeDurationMs) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < RESTORE_FIXED_SIZE) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_RESTORE_COLORS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  return RESTORE_FIXED_SIZE;
}

// Build a MSG_OVERRIDE_BRIGHTNESS frame. `brightness` clamped to
// [kBrightnessOverrideMin, 100] — values outside are rejected (returns 0),
// not silently clamped, so callers can spot bugs.
inline size_t buildOverrideBrightness(uint8_t* buf, size_t bufLen, uint16_t seq,
                                      const uint8_t sourceMac[6],
                                      const uint8_t targetMac[6],
                                      OverrideSurface surface,
                                      OverrideSource sourceKind,
                                      uint16_t fadeDurationMs,
                                      uint8_t brightness) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < OVERRIDE_BRIGHTNESS_FIXED_SIZE) return 0;
  if (brightness < kBrightnessOverrideMin || brightness > 100) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_OVERRIDE_BRIGHTNESS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  buf[22] = brightness;
  return OVERRIDE_BRIGHTNESS_FIXED_SIZE;
}

// Build a MSG_RESTORE_BRIGHTNESS frame. Same layout as RESTORE_COLORS.
inline size_t buildRestoreBrightness(uint8_t* buf, size_t bufLen, uint16_t seq,
                                     const uint8_t sourceMac[6],
                                     const uint8_t targetMac[6],
                                     OverrideSurface surface,
                                     OverrideSource sourceKind,
                                     uint16_t fadeDurationMs) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < RESTORE_FIXED_SIZE) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_RESTORE_BRIGHTNESS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  return RESTORE_FIXED_SIZE;
}

inline bool parseOverrideColors(const uint8_t* data, size_t len,
                                ParsedOverrideColors& out) {
  if (inspect(data, len) != MSG_OVERRIDE_COLORS) return false;
  if (len < OVERRIDE_COLORS_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  const uint8_t numColors = data[22];
  if (numColors < 1 || numColors > kMaxOverrideColorsPerFrame) return false;
  const size_t expected = OVERRIDE_COLORS_FIXED_SIZE + numColors * 4u;
  if (len != expected) return false;  // exact-match — silent drop on length mismatch
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  out.numColors = numColors;
  std::memcpy(out.colors, &data[OVERRIDE_COLORS_FIXED_SIZE], numColors * 4u);
  return true;
}

inline bool parseRestoreColors(const uint8_t* data, size_t len,
                               ParsedRestoreColors& out) {
  if (inspect(data, len) != MSG_RESTORE_COLORS) return false;
  if (len < RESTORE_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  return true;
}

inline bool parseOverrideBrightness(const uint8_t* data, size_t len,
                                    ParsedOverrideBrightness& out) {
  if (inspect(data, len) != MSG_OVERRIDE_BRIGHTNESS) return false;
  if (len < OVERRIDE_BRIGHTNESS_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  const uint8_t brightness = data[22];
  if (brightness < kBrightnessOverrideMin || brightness > 100) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  out.brightness = brightness;
  return true;
}

inline bool parseRestoreBrightness(const uint8_t* data, size_t len,
                                   ParsedRestoreBrightness& out) {
  if (inspect(data, len) != MSG_RESTORE_BRIGHTNESS) return false;
  if (len < RESTORE_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  return true;
}

}  // namespace lamp_protocol
