#pragma once

// MSG_COLOR_QUERY (0x32) + MSG_COLOR_INFO (0x33). Wire layout in docs/dev/networking.md.
// No gossip relay. addressedToUs filter on recv.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lampos/protocol/header.hpp>

namespace lamp_protocol {

constexpr size_t  COLOR_QUERY_SIZE        = HEADER_SIZE + 6 + 6;          // 18
constexpr size_t  COLOR_INFO_FIXED_PREFIX = HEADER_SIZE + 6 + 6;          // 18
constexpr uint8_t COLOR_INFO_MAX_STOPS    = 8;
constexpr size_t  COLOR_INFO_MAX_SIZE =
    COLOR_INFO_FIXED_PREFIX + 1 + COLOR_INFO_MAX_STOPS * 4 +
    1 + COLOR_INFO_MAX_STOPS * 4;                                           // 84

static_assert(COLOR_QUERY_SIZE == 18, "COLOR_QUERY size lock");
static_assert(COLOR_INFO_MAX_SIZE == 84, "COLOR_INFO max size lock");

struct ParsedColorQuery {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  targetMac[6];
};

// baseStops / shadeStops point into the recv buffer; each stop is 4 bytes RGBW.
// Caller must not retain past the parse call.
struct ParsedColorInfo {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  uint8_t        baseCount;
  const uint8_t* baseStops;
  uint8_t        shadeCount;
  const uint8_t* shadeStops;
};

inline size_t buildColorQuery(uint8_t* buf, size_t bufLen, uint16_t seq,
                              const uint8_t sourceMac[6],
                              const uint8_t targetMac[6]) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < COLOR_QUERY_SIZE) return 0;
  detail::writeHeader(buf, MSG_COLOR_QUERY, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  return COLOR_QUERY_SIZE;
}

inline bool parseColorQuery(const uint8_t* data, size_t len,
                            ParsedColorQuery& out) {
  if (inspect(data, len) != MSG_COLOR_QUERY) return false;
  if (len < COLOR_QUERY_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  return true;
}

inline size_t buildColorInfo(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t sourceMac[6],
                             const uint8_t targetMac[6],
                             const uint8_t* baseStops, uint8_t baseCount,
                             const uint8_t* shadeStops, uint8_t shadeCount) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (baseCount > COLOR_INFO_MAX_STOPS) return 0;
  if (shadeCount > COLOR_INFO_MAX_STOPS) return 0;
  if (baseCount && !baseStops) return 0;
  if (shadeCount && !shadeStops) return 0;
  const size_t total = COLOR_INFO_FIXED_PREFIX + 1 + static_cast<size_t>(baseCount) * 4 +
                       1 + static_cast<size_t>(shadeCount) * 4;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_COLOR_INFO, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  size_t off = COLOR_INFO_FIXED_PREFIX;
  buf[off++] = baseCount;
  if (baseCount) {
    std::memcpy(&buf[off], baseStops, static_cast<size_t>(baseCount) * 4);
    off += static_cast<size_t>(baseCount) * 4;
  }
  buf[off++] = shadeCount;
  if (shadeCount) {
    std::memcpy(&buf[off], shadeStops, static_cast<size_t>(shadeCount) * 4);
    off += static_cast<size_t>(shadeCount) * 4;
  }
  return off;
}

inline bool parseColorInfo(const uint8_t* data, size_t len,
                           ParsedColorInfo& out) {
  if (inspect(data, len) != MSG_COLOR_INFO) return false;
  if (len < COLOR_INFO_FIXED_PREFIX + 1) return false;
  size_t off = COLOR_INFO_FIXED_PREFIX;
  const uint8_t baseCount = data[off++];
  if (baseCount > COLOR_INFO_MAX_STOPS) return false;
  if (len < off + static_cast<size_t>(baseCount) * 4 + 1) return false;
  const uint8_t* baseStops = baseCount ? &data[off] : nullptr;
  off += static_cast<size_t>(baseCount) * 4;
  const uint8_t shadeCount = data[off++];
  if (shadeCount > COLOR_INFO_MAX_STOPS) return false;
  if (len < off + static_cast<size_t>(shadeCount) * 4) return false;
  const uint8_t* shadeStops = shadeCount ? &data[off] : nullptr;

  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.baseCount  = baseCount;
  out.baseStops  = baseStops;
  out.shadeCount = shadeCount;
  out.shadeStops = shadeStops;
  return true;
}

}  // namespace lamp_protocol
