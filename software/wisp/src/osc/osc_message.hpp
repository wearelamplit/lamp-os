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

#include "paint/current_palette.hpp"  // PaletteColor

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

// Floats of the first message in the packet addressed exactly `address`
// whose arguments read whole. False when none does or the packet is malformed.
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

// A float layout naming more channels than this is refused.
inline constexpr size_t kOscLayoutMaxChannels = 16;
// Amber mixes into red at full and into green at this share (amber read as
// #FFBF00). Too low reads amber as red, too high as yellow.
inline constexpr float kOscAmberGreen = 0.75f;

// A palette message's colours, the format told by its type tags:
// - every tag `i`: one 0xRRGGBB per int, white 0;
// - every tag `f` or `i` otherwise: RGB in threes, white 0;
// - `s` then only `f` / `i`: a Limbic float layout. The string names one
//   channel per float (`am` and `uv` read before single letters; r g b w,
//   `a` amber, `u` UV, any other byte unused) and the floats repeat in groups
//   of its width. w drives white, amber mixes into red and green, UV and
//   unused channels drop.
// Channels land clamped to 0..1. False, with nothing to use in `out`, on
// any other tags, a NaN or infinite float, arguments that end early or run
// past the tags, a float count the group width does not divide, an empty or
// over-long layout, no colour, or more than `cap` colours.
bool oscReadColours(const OscMessage& msg, PaletteColor* out, size_t cap,
                    size_t& count);

enum class OscColours : uint8_t {
  Absent,   // no message addressed exactly `address`, or a malformed packet
  Read,     // out/count hold the first such message's colours
  Refused,  // such messages came and none read whole
};

// The colours of the first message in the packet addressed exactly
// `address` that oscReadColours reads. On Refused, `refusedTags` points at
// the last refused message's type tags, inside the packet.
OscColours oscFindColours(const uint8_t* data, size_t len, const char* address,
                          PaletteColor* out, size_t cap, size_t& count,
                          const char*& refusedTags);

}  // namespace wisp
