#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lampos/clock.hpp>

namespace lamp {

// Who asked for a one-shot transient expression.
enum class TransientSource : unsigned char {
  Local,   // this lamp: the app's Test button, a greeting
  Remote,  // a peer: a cascade command or an echo of a peer's event
};

// Slots for in-flight transients. Remote ones may fill only kMaxRemote, so
// a crowd's echoes and cascades always leave room for the lamp's own.
struct TransientSlots {
  static constexpr size_t kMax = 4;
  static constexpr size_t kMaxRemote = 2;
};

// True when an invocation needing `targets` instances may start: the slots
// its source may use have room for all of them, and `heapOk` (the largest
// free block holds them plus headroom) so every instance that starts can
// finish allocating.
constexpr bool admitTransient(size_t liveTotal, size_t liveRemote, size_t targets,
                              TransientSource source, bool heapOk) {
  if (!heapOk) return false;
  if (liveTotal + targets > TransientSlots::kMax) return false;
  return source == TransientSource::Local ||
         liveRemote + targets <= TransientSlots::kMaxRemote;
}

// True when a triggered invocation fans out to the mesh: one this lamp
// originates (`broadcast`) goes out whether or not it played here, when this
// lamp asked for it (the app's Test, a greeting), so a lamp short of room
// still sends the wave; a peer's never does.
constexpr bool cascadesFrom(bool broadcast, bool playedHere, TransientSource source) {
  return broadcast && (playedHere || source == TransientSource::Local);
}

// After the lamp's own invocation of a type could not play (no room), that
// type waits kMs before it is tried again, so a Test tapped repeatedly while
// the lamp is short neither retries the admission nor resends its wave each
// tap. Holds the last kSlots types refused.
class LocalCooldown {
 public:
  static constexpr uint32_t kMs = 1000;
  static constexpr size_t kSlots = 4;
  static constexpr size_t kTypeLen = 15;

  bool waiting(const char* type, uint32_t nowMs) const {
    for (const Slot& s : slots_) {
      if (s.used && std::strncmp(s.type, type, kTypeLen) == 0 && nowMs - s.atMs < kMs) {
        return true;
      }
    }
    return false;
  }
  // Ends cooldowns that have run out, so none reads as running again one
  // millis() wrap later. Call at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs) {
    for (Slot& s : slots_) {
      if (s.used && lampos::msSince(nowMs, s.atMs) >= kMs) s.used = false;
    }
  }
  void refused(const char* type, uint32_t nowMs) {
    Slot& s = slots_[next_];
    next_ = (next_ + 1) % kSlots;
    std::strncpy(s.type, type, kTypeLen);
    s.type[kTypeLen] = '\0';
    s.atMs = nowMs;
    s.used = true;
  }

 private:
  struct Slot {
    char type[kTypeLen + 1] = {};
    uint32_t atMs = 0;
    bool used = false;
  };
  Slot slots_[kSlots];
  size_t next_ = 0;
};

// admitTransient over the live transients themselves (anything with a
// `source` member), so the call site passes only what it knows.
template <class Transients>
bool admitTransientAmong(const Transients& live, size_t targets, TransientSource source,
                         bool heapOk) {
  size_t remote = 0;
  for (const auto& t : live) {
    if (t.source == TransientSource::Remote) ++remote;
  }
  return admitTransient(live.size(), remote, targets, source, heapOk);
}

}  // namespace lamp
