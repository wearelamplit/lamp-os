// Native-test stub for mbedtls/sha256.h.
//
// firmware_signature.cpp's streaming-verify path includes <mbedtls/sha256.h>
// to use mbedtls_sha256_{init,starts,update,finish,free}. On-device, that
// header comes from the IDF mbedtls component; on the host (PIO `native`
// env), there's no mbedtls install — so the test rig adds this directory
// to the include path and ships a self-contained, public-domain SHA-256
// implementation that mirrors the subset of mbedtls's API
// the production code actually uses.
//
// The SHA-256 implementation is byte-for-byte equivalent to FIPS 180-4 §6.2,
// derived from the public-domain "sha256.c" reference by Brad Conte:
//   https://github.com/B-Con/crypto-algorithms/blob/master/sha256.c
// vendored inline here so the host test binary has zero external deps.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// mbedtls's API shape — these are the only entry points firmware_signature
// uses from this header, so we don't need to mirror the rest.
typedef struct {
  uint8_t  data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} mbedtls_sha256_context;

namespace mbedtls_sha256_stub_detail {

constexpr uint32_t kInitState[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

inline void transform(mbedtls_sha256_context* ctx, const uint8_t data[64]) {
  uint32_t w[64];
  for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
    w[i] = (uint32_t(data[j]) << 24) | (uint32_t(data[j + 1]) << 16) |
           (uint32_t(data[j + 2]) << 8) | uint32_t(data[j + 3]);
  }
  for (uint32_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];

  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + S1 + ch + kRoundConstants[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

}  // namespace mbedtls_sha256_stub_detail

inline void mbedtls_sha256_init(mbedtls_sha256_context* ctx) {
  std::memset(ctx, 0, sizeof(*ctx));
}

inline void mbedtls_sha256_free(mbedtls_sha256_context* ctx) {
  if (ctx) std::memset(ctx, 0, sizeof(*ctx));
}

inline int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int is224) {
  if (is224) return -1;  // production only uses SHA-256
  ctx->datalen = 0;
  ctx->bitlen = 0;
  for (int i = 0; i < 8; ++i)
    ctx->state[i] = mbedtls_sha256_stub_detail::kInitState[i];
  return 0;
}

inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx,
                                 const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      mbedtls_sha256_stub_detail::transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
  return 0;
}

inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, uint8_t out[32]) {
  uint32_t i = ctx->datalen;
  // Pad whatever data is left in the buffer.
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    mbedtls_sha256_stub_detail::transform(ctx, ctx->data);
    std::memset(ctx->data, 0, 56);
  }
  // Append total bit length as 64-bit big-endian.
  ctx->bitlen += static_cast<uint64_t>(ctx->datalen) * 8u;
  ctx->data[63] = static_cast<uint8_t>(ctx->bitlen);
  ctx->data[62] = static_cast<uint8_t>(ctx->bitlen >> 8);
  ctx->data[61] = static_cast<uint8_t>(ctx->bitlen >> 16);
  ctx->data[60] = static_cast<uint8_t>(ctx->bitlen >> 24);
  ctx->data[59] = static_cast<uint8_t>(ctx->bitlen >> 32);
  ctx->data[58] = static_cast<uint8_t>(ctx->bitlen >> 40);
  ctx->data[57] = static_cast<uint8_t>(ctx->bitlen >> 48);
  ctx->data[56] = static_cast<uint8_t>(ctx->bitlen >> 56);
  mbedtls_sha256_stub_detail::transform(ctx, ctx->data);
  // Emit big-endian 32-byte digest.
  for (uint32_t w = 0; w < 8; ++w) {
    out[w * 4 + 0] = static_cast<uint8_t>(ctx->state[w] >> 24);
    out[w * 4 + 1] = static_cast<uint8_t>(ctx->state[w] >> 16);
    out[w * 4 + 2] = static_cast<uint8_t>(ctx->state[w] >> 8);
    out[w * 4 + 3] = static_cast<uint8_t>(ctx->state[w]);
  }
  return 0;
}
