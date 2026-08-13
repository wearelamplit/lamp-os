#include "config/crypto.hpp"

#include <cstring>

namespace wisp { namespace crypto {

uint8_t magicByte(const uint8_t* p, size_t n) {
  return n ? p[0] : 0;
}

bool decryptOp(const uint8_t* p, size_t n,
               const uint8_t* uuidLE16, const char* name,
               const std::string& password,
               RecentNonces& nonces, std::string& out) {
  out.clear();
  if (n < 1 + NONCE_LEN + TAG_LEN) return false;
  if (p[0] != MAGIC_CIPHERTEXT) return false;
  if (password.empty()) return false;

  std::array<uint8_t, NONCE_LEN> nonceArr{};
  std::memcpy(nonceArr.data(), p + 1, NONCE_LEN);
  for (const auto& seen : nonces.entries) {
    if (seen == nonceArr) return false;
  }

  if (!lampos::crypto::decryptPayload(p, n, uuidLE16, name, password, out)) {
    return false;
  }

  nonces.entries.push_back(nonceArr);
  if (nonces.entries.size() > MAX_RECENT_NONCES) {
    nonces.entries.pop_front();
  }
  return true;
}

}}  // namespace wisp::crypto
