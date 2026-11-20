#include "ssid_presence.hpp"

#include <algorithm>

namespace wifi {

namespace {
uint32_t fnv1a(std::string_view s) {
  uint32_t h = 0x811C9DC5u;
  for (char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}
}  // namespace

void SsidPresence::add(std::string_view ssid) {
  if (ssid.empty()) return;
  const uint32_t h = fnv1a(ssid);
  const auto end = hashes_.begin() + count_;
  if (std::find(hashes_.begin(), end, h) != end) return;
  if (count_ == kCapacity) {
    overflowed_ = true;
    return;
  }
  hashes_[count_++] = h;
}

bool SsidPresence::contains(std::string_view ssid) const {
  if (ssid.empty()) return false;
  const uint32_t h = fnv1a(ssid);
  const auto end = hashes_.begin() + count_;
  return std::find(hashes_.begin(), end, h) != end;
}

}  // namespace wifi
