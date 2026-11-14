#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wifi {

// The SSIDs one scan saw, kept without heap for home presence. Feed a scan
// strongest first: up to kCapacity distinct SSIDs are kept, and past that the
// weaker ones are dropped and overflowed() says so. contains() can answer true
// for an SSID never added whose 32-bit hash matches a kept one (odds at most
// kCapacity in 2^32). An empty SSID is never present.
class SsidPresence {
 public:
  // ponytail: 64 distinct SSIDs; a home AP weaker than 64 others reads as away.
  // Raise it if the LAMP_DEBUG scan log reports an overflow.
  static constexpr size_t kCapacity = 64;

  void add(std::string_view ssid);
  bool contains(std::string_view ssid) const;
  size_t size() const { return count_; }
  bool overflowed() const { return overflowed_; }

 private:
  std::array<uint32_t, kCapacity> hashes_{};
  size_t count_ = 0;
  bool overflowed_ = false;
};

}  // namespace wifi
