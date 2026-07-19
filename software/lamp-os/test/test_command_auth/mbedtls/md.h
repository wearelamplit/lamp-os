// Native-test stub for mbedtls/md.h.
//
// command_auth.cpp uses mbedtls_md_hmac(SHA256, ...) on device (IDF mbedtls
// component). The host (PIO `native`) env has no mbedtls install, so this
// directory is added to the include path and ships the HMAC-SHA256 subset the
// production code touches: mbedtls_md_info_from_type, mbedtls_md_get_type,
// mbedtls_md_hmac. The SHA-256 core is the same public-domain FIPS 180-4 §6.2
// implementation used by the other crypto test shims; HMAC is the RFC 2104
// construction over it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

typedef enum {
  MBEDTLS_MD_NONE = 0,
  MBEDTLS_MD_SHA256,
} mbedtls_md_type_t;

typedef struct {
  mbedtls_md_type_t type;
} mbedtls_md_info_t;

namespace mbedtls_md_stub_detail {

typedef struct {
  uint8_t  data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} sha256_ctx;

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

inline void transform(sha256_ctx* ctx, const uint8_t data[64]) {
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

  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
  uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

  for (uint32_t i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t temp1 = h + S1 + ch + kRoundConstants[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }

  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
  ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

inline void sha256_init(sha256_ctx* ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  for (int i = 0; i < 8; ++i) ctx->state[i] = kInitState[i];
}

inline void sha256_update(sha256_ctx* ctx, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

inline void sha256_finish(sha256_ctx* ctx, uint8_t out[32]) {
  uint32_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    transform(ctx, ctx->data);
    std::memset(ctx->data, 0, 56);
  }
  ctx->bitlen += static_cast<uint64_t>(ctx->datalen) * 8u;
  for (int b = 0; b < 8; ++b)
    ctx->data[63 - b] = static_cast<uint8_t>(ctx->bitlen >> (8 * b));
  transform(ctx, ctx->data);
  for (uint32_t w = 0; w < 8; ++w) {
    out[w * 4 + 0] = static_cast<uint8_t>(ctx->state[w] >> 24);
    out[w * 4 + 1] = static_cast<uint8_t>(ctx->state[w] >> 16);
    out[w * 4 + 2] = static_cast<uint8_t>(ctx->state[w] >> 8);
    out[w * 4 + 3] = static_cast<uint8_t>(ctx->state[w]);
  }
}

inline void sha256(const uint8_t* in, size_t len, uint8_t out[32]) {
  sha256_ctx ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, in, len);
  sha256_finish(&ctx, out);
}

}  // namespace mbedtls_md_stub_detail

inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t t) {
  static const mbedtls_md_info_t sha256_info{MBEDTLS_MD_SHA256};
  return t == MBEDTLS_MD_SHA256 ? &sha256_info : nullptr;
}

inline int mbedtls_md_hmac(const mbedtls_md_info_t* info,
                           const uint8_t* key, size_t keylen,
                           const uint8_t* input, size_t ilen,
                           uint8_t* output) {
  namespace d = mbedtls_md_stub_detail;
  if (!info || info->type != MBEDTLS_MD_SHA256) return -1;

  uint8_t k[64] = {};
  if (keylen > 64) {
    d::sha256(key, keylen, k);
  } else {
    std::memcpy(k, key, keylen);
  }

  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }

  uint8_t inner[32];
  d::sha256_ctx ctx;
  d::sha256_init(&ctx);
  d::sha256_update(&ctx, ipad, 64);
  d::sha256_update(&ctx, input, ilen);
  d::sha256_finish(&ctx, inner);

  d::sha256_init(&ctx);
  d::sha256_update(&ctx, opad, 64);
  d::sha256_update(&ctx, inner, 32);
  d::sha256_finish(&ctx, output);
  return 0;
}
