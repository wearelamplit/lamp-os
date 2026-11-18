// OSC 1.0 packet reader over a caller-owned buffer. No heap: every view
// points into the packet, which must outlive the callback.
// https://opensoundcontrol.stanford.edu/spec-1_0.html
//
// Reads messages and bundles (nested up to kOscMaxBundleDepth). The bundle
// time tag is ignored: every element is applied on arrival. Argument types
// understood: f, i, s. Anything else fails oscFloatArgs.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

inline constexpr int kOscMaxBundleDepth = 4;

struct OscMessage {
  const char* address = nullptr;  // NUL-terminated, inside the packet
  const char* tags = nullptr;     // type tags after the leading ','
  const uint8_t* args = nullptr;
  size_t argsLen = 0;
};

// One bare message. False on any framing fault.
bool oscParseMessage(const uint8_t* data, size_t len, OscMessage& out);

using OscMessageFn = void (*)(const OscMessage& msg, void* ctx);

// Calls `fn` for each message in a bare message or a bundle. Returns false
// on the first framing fault; messages before it have already been delivered.
bool oscForEachMessage(const uint8_t* data, size_t len, OscMessageFn fn,
                       void* ctx);

// Reads every numeric argument as a float (`i` converts), skipping `s`.
// False on an unknown type tag, truncated arguments, more than `cap`, or a
// NaN or infinite float.
bool oscFloatArgs(const OscMessage& msg, float* out, size_t cap,
                  size_t& count);

// True when the packet parses and some message's address starts with
// `prefix`.
bool oscHasAddressPrefix(const uint8_t* data, size_t len, const char* prefix);

// Floats of the first message in the packet addressed exactly `address`.
// False when no such message parses or the packet is malformed.
bool oscFindFloats(const uint8_t* data, size_t len, const char* address,
                   float* out, size_t cap, size_t& count);

enum class OscFeedPacket : uint8_t {
  Palette,  // `address` parsed; its floats are in out/count
  Feed,     // other messages under `feedPrefix`, and no `address` message
  Other,    // anything else, including an `address` message that failed
};

// Sorts one packet for the palette listener. Only Feed may refresh a held
// palette's liveness: a palette for this role that failed to parse says
// nothing about whether the last good one is still current.
OscFeedPacket oscClassifyFeedPacket(const uint8_t* data, size_t len,
                                    const char* address,
                                    const char* feedPrefix, float* out,
                                    size_t cap, size_t& count);

}  // namespace wisp
