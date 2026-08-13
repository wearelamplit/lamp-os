#include "ota_channel.hpp"

#include <cstring>

// Returns a pointer to the last '-' in s, or nullptr if none.
static const char* lastDash(const char* s) {
  const char* p = nullptr;
  for (; *s; ++s) {
    if (*s == '-') p = s;
  }
  return p;
}

bool otaAcceptable(const char* ourChannel,
                   uint32_t    ourVersion,
                   const char* offerChannel,
                   uint32_t    offerVersion) {
  if (!ourChannel || !offerChannel || !ourChannel[0] || !offerChannel[0]) {
    return false;
  }

  const char* ourDash   = lastDash(ourChannel);
  const char* offerDash = lastDash(offerChannel);
  if (!ourDash || !offerDash) return false;

  // Type prefix lengths must match and be equal.
  const size_t ourPrefixLen   = static_cast<size_t>(ourDash   - ourChannel);
  const size_t offerPrefixLen = static_cast<size_t>(offerDash - offerChannel);
  if (ourPrefixLen != offerPrefixLen) return false;
  if (std::memcmp(ourChannel, offerChannel, ourPrefixLen) != 0) return false;

  // Type prefixes match. Compare channel suffixes.
  const char* ourSuffix   = ourDash   + 1;  // e.g. "beta" or "stable"
  const char* offerSuffix = offerDash + 1;

  const bool intraChannel = (std::strcmp(ourSuffix, offerSuffix) == 0);
  if (intraChannel) {
    return offerVersion > ourVersion;
  }

  // Promotion: beta lamp accepts stable offer when offerVersion >= ourVersion.
  if (std::strcmp(ourSuffix, "beta") == 0 &&
      std::strcmp(offerSuffix, "stable") == 0) {
    return offerVersion >= ourVersion;
  }

  return false;
}
