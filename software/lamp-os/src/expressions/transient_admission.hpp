#pragma once

#include <cstddef>
#include <cstdint>

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
