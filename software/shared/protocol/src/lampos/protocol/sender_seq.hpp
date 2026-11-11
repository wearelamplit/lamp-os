#pragma once

#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_random.h>
#endif

namespace lamp_protocol {

#if defined(ARDUINO) || defined(ESP_PLATFORM)
inline uint32_t seqEntropy() { return esp_random(); }
#else
// Native builds draw a boot's start from here, so a test can stage reboots.
inline uint32_t& nativeSeqEntropy() {
  static uint32_t value = 0;
  return value;
}
inline uint32_t seqEntropy() { return nativeSeqEntropy(); }
#endif

// The seq a node stamps on the frames it originates. Receivers drop a frame
// whose (sourceMac, msgType, seq) they still hold, and lamps already in the
// field hold those tuples with no age limit, so each boot starts at a random
// value. Seqs carry no order: a receiver may only compare them for equality.
// See docs/dev/networking.md ("Sequence numbers and dedup").
//
// The first next() draws the start, so it falls after the radio is up and
// esp_random() has real entropy. Callers serialize next() the way they
// would a bare counter.
class SenderSeq {
 public:
  uint16_t next() {
    if (!seeded_) {
      next_ = static_cast<uint16_t>(seqEntropy());
      seeded_ = true;
    }
    return next_++;
  }

 private:
  uint16_t next_ = 0;
  bool seeded_ = false;
};

}  // namespace lamp_protocol
