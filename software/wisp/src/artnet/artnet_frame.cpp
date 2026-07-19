#include "artnet/artnet_frame.hpp"

#include <cstring>

#include "paint/tuple_sampler.hpp"

namespace wisp {

namespace {
constexpr size_t kDmxStart = 18;
constexpr size_t kBytesPerFixture = 10;
}  // namespace

size_t buildArtnetDmxFrame(const CurrentPalette& palette, uint8_t seq,
                           uint8_t* out, size_t outLen) {
  if (outLen < kArtnetFrameSize) return 0;
  std::memset(out, 0, kArtnetFrameSize);

  // ART_NET_ID: 8 bytes including trailing NUL.
  static const char kId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};
  std::memcpy(out, kId, 8);

  // OpCode ART_DMX = 0x5000, little-endian.
  out[8] = 0x00;
  out[9] = 0x50;

  // ProtVer = 0x000E, big-endian. Lamp listener ignores; mirror what
  // Gen 1 senders typically emit.
  out[10] = 0x00;
  out[11] = 0x0E;

  out[12] = seq;
  out[13] = 0x00;  // physical

  // Universe 1, little-endian.
  out[14] = 0x01;
  out[15] = 0x00;

  // DMX payload length = 512, big-endian (matches lamp decode at
  // artnet.cpp:54: dmxDataLength = artnetPacket[17] | artnetPacket[16] << 8).
  out[16] = 0x02;
  out[17] = 0x00;

  for (uint8_t i = 0; i < kArtnetNumFixtures; ++i) {
    // Synthetic MAC: 02:57:49:53:50:<fixture>. The 0x02 sets the
    // locally-administered bit so it can't collide with a real OUI.
    // 0x57 0x49 0x53 0x50 = "WISP".
    uint8_t mac[6] = {0x02, 0x57, 0x49, 0x53, 0x50, i};
    ColorTuple t = sampleTupleForMac(palette, mac);
    uint8_t* f = out + kDmxStart + (i * kBytesPerFixture);
    // base = tuple[0], shade = tuple[1] (aligns with TupleSampler.h's
    // "[0] → base surface, [1] → shade surface" convention, and matches
    // PaintDistributor's mesh path, which puts t[0] as the primary base
    // color via OverrideSurface::Base). Without this, mixed-fleet rooms
    // (mesh + pre-mesh lamps) would show flipped surfaces for the same
    // palette. Channel layout is unchanged: 0..3 shade, 4..7 base.
    f[0] = t.r[1]; f[1] = t.g[1]; f[2] = t.b[1]; f[3] = t.w[1];
    f[4] = t.r[0]; f[5] = t.g[0]; f[6] = t.b[0]; f[7] = t.w[0];
    f[8] = 0;  // mode = ArtNet pass-through
    f[9] = 0;  // parameter (unused when mode = 0)
  }

  return kArtnetFrameSize;
}

}  // namespace wisp
