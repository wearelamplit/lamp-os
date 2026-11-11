#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lamp {

// When each expression type last played on this lamp, in fixed RAM. A type is
// keyed by its registry descriptor's address. A full table reuses its oldest
// slot. The owner calls retire() at least once per lampos::kRetiredAgeMs.
class RecentPlays {
 public:
  static constexpr size_t kSlots = 16;

  void record(const void* type, uint32_t nowMs);
  // True when `type` played less than windowMs before nowMs.
  bool playedWithin(const void* type, uint32_t windowMs, uint32_t nowMs) const;
  void retire(uint32_t nowMs);

 private:
  struct Stamp {
    const void* type = nullptr;
    uint32_t atMs = 0;
  };
  std::array<Stamp, kSlots> stamps_{};
};

}  // namespace lamp
