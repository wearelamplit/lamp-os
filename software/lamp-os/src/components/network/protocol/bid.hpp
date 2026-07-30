#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "command_auth.hpp"
#include <lampos/protocol/header.hpp>

// MSG_BID (0x34).
// Nearby-scoped (no relay) relationship-gated request, authenticated by an
// 8-byte command_auth trailer (a keyed lamp drops a bad/absent tag). The
// receiver resolves the sender's disposition + roster entry from sourceMac and
// decides probabilistically whether to honor. build/parse: buildBid / parseBid.
//
// Frame layout:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac (the bidder)
//   12    1    bidType (BidType enum; BID_GREETING is the only value)
//   13    8    command_auth tag (HMAC-SHA256 trailer; see command_auth.hpp)

namespace lamp_protocol {

enum BidType : uint8_t {
  BID_GREETING = 0x01,
};

constexpr size_t BID_FIXED_SIZE = HEADER_SIZE + 6 + 1;   // 13
constexpr size_t BID_TAG_SIZE   = command_auth::TAG_SIZE;  // 8
constexpr size_t BID_SIZE       = BID_FIXED_SIZE + BID_TAG_SIZE;  // 21

static_assert(BID_FIXED_SIZE == 13, "BID fixed size lock");
static_assert(BID_SIZE == 21,       "BID size lock");

struct ParsedBid {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        bidType;
  const uint8_t* tag;  // the 8-byte trailer; verify via command_auth::verify
};

// Build a MSG_BID frame body (header+sourceMac+bidType). Returns the body
// length WITHOUT the auth tag, 0 on error. The caller appends BID_TAG_SIZE
// tag bytes (command_auth::appendTag) before sending; parseBid requires them.
inline size_t buildBid(uint8_t* buf, size_t bufLen, uint16_t seq,
                       const uint8_t sourceMac[6], uint8_t bidType) {
  if (!buf || !sourceMac) return 0;
  if (bufLen < BID_SIZE) return 0;
  detail::writeHeader(buf, MSG_BID, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = bidType;
  return BID_FIXED_SIZE;
}

// Parse a MSG_BID frame. `out.tag` points into `data`; valid only while `data`
// is live. The 8-byte auth tag is stripped from the body.
inline bool parseBid(const uint8_t* data, size_t len, ParsedBid& out) {
  if (inspect(data, len) != MSG_BID) return false;
  if (len != BID_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.bidType = data[12];
  out.tag     = &data[BID_FIXED_SIZE];
  return true;
}

}  // namespace lamp_protocol
