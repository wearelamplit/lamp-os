#include "command_auth.hpp"

#include <mbedtls/md.h>

#include <cstring>

namespace lamp_protocol { namespace command_auth {

namespace {

constexpr size_t kKeyLen = 32;

uint8_t  s_key[kKeyLen] = {};
bool     s_hasKey       = false;
bool     s_inited       = false;

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parse LAMP_COMMAND_KEY_HEX (a 64-char hex string literal injected at build
// time) into s_key. Anything malformed or absent leaves the module keyless.
void ensureInit() {
  if (s_inited) return;
  s_inited = true;
#ifdef LAMP_COMMAND_KEY_HEX
  const char* hex = LAMP_COMMAND_KEY_HEX;
  if (std::strlen(hex) != kKeyLen * 2) return;
  for (size_t i = 0; i < kKeyLen; ++i) {
    const int hi = hexNibble(hex[i * 2]);
    const int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return;
    s_key[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  s_hasKey = true;
#endif
}

// HMAC-SHA256(s_key, in[0..inLen)) -> out[32]. Returns false on any mbedTLS
// error. Caller guarantees s_hasKey.
bool hmac(const uint8_t* in, size_t inLen, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  return mbedtls_md_hmac(info, s_key, kKeyLen, in, inLen, out) == 0;
}

}  // namespace

void init() {
  ensureInit();
}

bool enabled() {
  ensureInit();
  return s_hasKey;
}

size_t appendTag(uint8_t* buf, size_t bodyLen, size_t bufCap) {
  ensureInit();
  if (!buf || bufCap < bodyLen + TAG_SIZE) return bodyLen;
  if (!s_hasKey) {
    std::memset(buf + bodyLen, 0, TAG_SIZE);
    return bodyLen + TAG_SIZE;
  }
  uint8_t digest[32];
  if (!hmac(buf, bodyLen, digest)) {
    std::memset(buf + bodyLen, 0, TAG_SIZE);
    return bodyLen + TAG_SIZE;
  }
  std::memcpy(buf + bodyLen, digest, TAG_SIZE);
  return bodyLen + TAG_SIZE;
}

bool verify(const uint8_t* body, size_t bodyLen, const uint8_t* tag) {
  ensureInit();
  if (!s_hasKey) return true;
  if (!body || !tag) return false;
  uint8_t digest[32];
  if (!hmac(body, bodyLen, digest)) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < TAG_SIZE; ++i) diff |= digest[i] ^ tag[i];
  return diff == 0;
}

void loadKeyForTest(const uint8_t key[32]) {
  std::memcpy(s_key, key, kKeyLen);
  s_hasKey = true;
  s_inited = true;
}

void clearKeyForTest() {
  s_hasKey = false;
  s_inited = true;
}

}}  // namespace lamp_protocol::command_auth
