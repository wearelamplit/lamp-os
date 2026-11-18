#include "osc/osc_message.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wisp {

namespace {

constexpr char kBundleTag[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'};
constexpr size_t kBundleHeaderLen = 16;  // "#bundle\0" + 8-byte time tag

uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Padded size of the OSC-string at data[pos], or 0 if it is not terminated
// inside the packet.
size_t stringSpan(const uint8_t* data, size_t len, size_t pos) {
  const void* nul = std::memchr(data + pos, 0, len - pos);
  if (!nul) return 0;
  const size_t n = static_cast<const uint8_t*>(nul) - (data + pos);
  const size_t padded = (n + 4) & ~static_cast<size_t>(3);
  return pos + padded <= len ? padded : 0;
}

bool walk(const uint8_t* data, size_t len, OscMessageFn fn, void* ctx,
          int depth) {
  if (len < 4 || len % 4 != 0) return false;
  if (data[0] == '/') {
    OscMessage m;
    if (!oscParseMessage(data, len, m)) return false;
    fn(m, ctx);
    return true;
  }
  if (len < kBundleHeaderLen || std::memcmp(data, kBundleTag, 8) != 0)
    return false;
  if (depth >= kOscMaxBundleDepth) return false;
  size_t pos = kBundleHeaderLen;
  while (pos < len) {
    if (len - pos < 4) return false;
    const uint32_t size = readU32(data + pos);
    pos += 4;
    if (size == 0 || size > len - pos) return false;
    if (!walk(data + pos, size, fn, ctx, depth + 1)) return false;
    pos += size;
  }
  return true;
}

}  // namespace

bool oscParseMessage(const uint8_t* data, size_t len, OscMessage& out) {
  if (!data || len < 8 || len % 4 != 0 || data[0] != '/') return false;
  const size_t addrSpan = stringSpan(data, len, 0);
  if (addrSpan == 0 || addrSpan >= len) return false;
  if (data[addrSpan] != ',') return false;
  const size_t tagSpan = stringSpan(data, len, addrSpan);
  if (tagSpan == 0) return false;
  out.address = reinterpret_cast<const char*>(data);
  out.tags = reinterpret_cast<const char*>(data + addrSpan + 1);
  out.args = data + addrSpan + tagSpan;
  out.argsLen = len - addrSpan - tagSpan;
  return true;
}

bool oscForEachMessage(const uint8_t* data, size_t len, OscMessageFn fn,
                       void* ctx) {
  if (!data || !fn) return false;
  return walk(data, len, fn, ctx, 0);
}

bool oscFloatArgs(const OscMessage& msg, float* out, size_t cap,
                  size_t& count) {
  count = 0;
  size_t pos = 0;
  for (const char* t = msg.tags; *t; ++t) {
    switch (*t) {
      case 'f':
      case 'i': {
        if (msg.argsLen - pos < 4 || count >= cap) return false;
        const uint32_t raw = readU32(msg.args + pos);
        float v;
        if (*t == 'f') {
          std::memcpy(&v, &raw, sizeof v);
          if (!std::isfinite(v)) return false;
        } else {
          v = static_cast<float>(static_cast<int32_t>(raw));
        }
        out[count++] = v;
        pos += 4;
        break;
      }
      case 's': {
        if (pos >= msg.argsLen) return false;
        const size_t span = stringSpan(msg.args, msg.argsLen, pos);
        if (span == 0) return false;
        pos += span;
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

namespace {

struct FindCtx {
  const char* address;
  float* out;
  size_t cap;
  size_t count;
  bool found;
};

void findOne(const OscMessage& m, void* raw) {
  auto* ctx = static_cast<FindCtx*>(raw);
  if (ctx->found || std::strcmp(m.address, ctx->address) != 0) return;
  size_t n = 0;
  if (!oscFloatArgs(m, ctx->out, ctx->cap, n)) return;
  ctx->count = n;
  ctx->found = true;
}

}  // namespace

namespace {

struct PrefixCtx {
  const char* prefix;
  size_t prefixLen;
  bool found;
};

void matchPrefix(const OscMessage& m, void* raw) {
  auto* ctx = static_cast<PrefixCtx*>(raw);
  if (std::strncmp(m.address, ctx->prefix, ctx->prefixLen) == 0) {
    ctx->found = true;
  }
}

}  // namespace

bool oscHasAddressPrefix(const uint8_t* data, size_t len, const char* prefix) {
  if (!prefix) return false;
  PrefixCtx ctx{prefix, std::strlen(prefix), false};
  const bool ok = oscForEachMessage(data, len, matchPrefix, &ctx);
  return ok && ctx.found;
}

bool oscFindFloats(const uint8_t* data, size_t len, const char* address,
                   float* out, size_t cap, size_t& count) {
  FindCtx ctx{address, out, cap, 0, false};
  const bool ok = oscForEachMessage(data, len, findOne, &ctx);
  count = ctx.count;
  return ok && ctx.found;
}

namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// The 4-byte numeric argument at `pos` as a float, `i` converted. False on a
// NaN or infinite float.
bool readNumber(const OscMessage& msg, char tag, size_t pos, float& out) {
  const uint32_t raw = readU32(msg.args + pos);
  if (tag == 'i') {
    out = static_cast<float>(static_cast<int32_t>(raw));
    return true;
  }
  std::memcpy(&out, &raw, sizeof out);
  return std::isfinite(out);
}

void readHex(const OscMessage& msg, size_t n, PaletteColor* out) {
  for (size_t i = 0; i < n; ++i) {
    const uint32_t v = readU32(msg.args + i * 4);
    out[i].r = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
    out[i].g = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
    out[i].b = static_cast<float>(v & 0xFF) / 255.0f;
    out[i].w = 0.0f;
  }
}

bool readRgb(const OscMessage& msg, size_t colours, PaletteColor* out) {
  for (size_t i = 0; i < colours; ++i) {
    float ch[3];
    for (size_t k = 0; k < 3; ++k) {
      const size_t arg = i * 3 + k;
      if (!readNumber(msg, msg.tags[arg], arg * 4, ch[k])) return false;
    }
    out[i].r = clamp01(ch[0]);
    out[i].g = clamp01(ch[1]);
    out[i].b = clamp01(ch[2]);
    out[i].w = 0.0f;
  }
  return true;
}

enum class LayoutChannel : uint8_t { Unused, Red, Green, Blue, White, Amber, Uv };

// The layout's channels into `out`; 0 for an empty or over-long layout.
size_t parseLayout(const char* s, LayoutChannel* out) {
  size_t width = 0;
  while (*s) {
    if (width == kOscLayoutMaxChannels) return 0;
    if ((s[0] == 'a' && s[1] == 'm') || (s[0] == 'u' && s[1] == 'v')) {
      out[width++] = s[0] == 'a' ? LayoutChannel::Amber : LayoutChannel::Uv;
      s += 2;
      continue;
    }
    switch (*s) {
      case 'r': out[width] = LayoutChannel::Red; break;
      case 'g': out[width] = LayoutChannel::Green; break;
      case 'b': out[width] = LayoutChannel::Blue; break;
      case 'w': out[width] = LayoutChannel::White; break;
      case 'a': out[width] = LayoutChannel::Amber; break;
      case 'u': out[width] = LayoutChannel::Uv; break;
      default:  out[width] = LayoutChannel::Unused; break;
    }
    ++width;
    ++s;
  }
  return width;
}

bool readLayout(const OscMessage& msg, PaletteColor* out, size_t cap,
                size_t& count) {
  const size_t span = stringSpan(msg.args, msg.argsLen, 0);
  if (span == 0) return false;
  LayoutChannel channels[kOscLayoutMaxChannels];
  const size_t width =
      parseLayout(reinterpret_cast<const char*>(msg.args), channels);
  if (width == 0) return false;
  const char* floatTags = msg.tags + 1;
  size_t n = 0;
  for (const char* t = floatTags; *t; ++t, ++n) {
    if (*t != 'f' && *t != 'i') return false;
  }
  if (n == 0 || n % width != 0 || msg.argsLen != span + n * 4) return false;
  const size_t colours = n / width;
  if (colours > cap) return false;
  for (size_t i = 0; i < colours; ++i) {
    float r = 0, g = 0, b = 0, w = 0, amber = 0;
    for (size_t k = 0; k < width; ++k) {
      const size_t arg = i * width + k;
      float v;
      if (!readNumber(msg, floatTags[arg], span + arg * 4, v)) return false;
      v = clamp01(v);
      switch (channels[k]) {
        case LayoutChannel::Red:   r = v; break;
        case LayoutChannel::Green: g = v; break;
        case LayoutChannel::Blue:  b = v; break;
        case LayoutChannel::White: w = v; break;
        case LayoutChannel::Amber: amber = v; break;
        case LayoutChannel::Uv:
        case LayoutChannel::Unused:
          break;
      }
    }
    out[i].r = clamp01(r + amber);
    out[i].g = clamp01(g + kOscAmberGreen * amber);
    out[i].b = b;
    out[i].w = w;
  }
  count = colours;
  return true;
}

}  // namespace

bool oscReadColours(const OscMessage& msg, PaletteColor* out, size_t cap,
                    size_t& count) {
  count = 0;
  if (!msg.tags || !out) return false;
  if (msg.tags[0] == 's') return readLayout(msg, out, cap, count);
  size_t n = 0;
  bool allInt = true;
  for (const char* t = msg.tags; *t; ++t, ++n) {
    if (*t != 'f' && *t != 'i') return false;
    allInt = allInt && *t == 'i';
  }
  if (n == 0 || msg.argsLen != n * 4) return false;
  const size_t colours = allInt ? n : n / 3;
  if (!allInt && n % 3 != 0) return false;
  if (colours > cap) return false;
  if (allInt) {
    readHex(msg, n, out);
  } else if (!readRgb(msg, colours, out)) {
    return false;
  }
  count = colours;
  return true;
}

namespace {

struct ColoursCtx {
  const char* address;
  PaletteColor* out;
  size_t cap;
  size_t count;
  OscColours result;
  const char* refusedTags;
};

void findColours(const OscMessage& m, void* raw) {
  auto* ctx = static_cast<ColoursCtx*>(raw);
  if (ctx->result == OscColours::Read ||
      std::strcmp(m.address, ctx->address) != 0) {
    return;
  }
  size_t n = 0;
  if (oscReadColours(m, ctx->out, ctx->cap, n)) {
    ctx->count = n;
    ctx->result = OscColours::Read;
  } else {
    ctx->result = OscColours::Refused;
    ctx->refusedTags = m.tags;
  }
}

}  // namespace

OscColours oscFindColours(const uint8_t* data, size_t len, const char* address,
                          PaletteColor* out, size_t cap, size_t& count,
                          const char*& refusedTags) {
  ColoursCtx ctx{address, out, cap, 0, OscColours::Absent, nullptr};
  count = 0;
  refusedTags = nullptr;
  if (!address || !oscForEachMessage(data, len, findColours, &ctx)) {
    return OscColours::Absent;
  }
  count = ctx.count;
  refusedTags = ctx.refusedTags;
  return ctx.result;
}

OscFeedPacket oscClassifyFeedPacket(const uint8_t* data, size_t len,
                                    const char* address,
                                    const char* feedPrefix, float* out,
                                    size_t cap, size_t& count) {
  if (oscFindFloats(data, len, address, out, cap, count)) {
    return OscFeedPacket::Palette;
  }
  count = 0;
  if (oscHasAddressPrefix(data, len, address)) return OscFeedPacket::Other;
  return oscHasAddressPrefix(data, len, feedPrefix) ? OscFeedPacket::Feed
                                                    : OscFeedPacket::Other;
}

}  // namespace wisp
