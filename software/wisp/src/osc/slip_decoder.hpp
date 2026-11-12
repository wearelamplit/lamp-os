// RFC 1055 SLIP decoder sharing a byte stream with the text console.
//
// Outside a frame every byte is text except END, which opens one. Inside,
// bytes collect until the next END. Senders must lead each frame with END
// (double-ended SLIP, as OSC 1.1 recommends); a frame that only trails END
// reads as text. An empty frame (END END) keeps collecting, so back-to-back
// double-ended frames work. An oversize or badly escaped frame is discarded
// through its closing END, then the decoder is back on text. A frame that
// goes kIdleResetMs without a byte is dropped and the decoder is back on text,
// so a sender cut off mid-frame (or a stray END) cannot leave the console
// deaf.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class SlipDecoder {
 public:
  static constexpr uint8_t kEnd = 0xC0;
  static constexpr uint8_t kEsc = 0xDB;
  static constexpr uint8_t kEscEnd = 0xDC;
  static constexpr uint8_t kEscEsc = 0xDD;
  static constexpr size_t kMaxFrame = 1500;
  static constexpr uint32_t kIdleResetMs = 200;

  enum class Result : uint8_t { Text, Consumed, Frame };

  // Frame: frame()/frameLen() hold a complete frame until the next feed().
  Result feed(uint8_t b, uint32_t nowMs);
  // Retires the last-byte stamp, so a partial frame left for weeks still resets
  // on its next byte. Call at least once per lampos::kRetiredAgeMs.
  void retire(uint32_t nowMs);

  const uint8_t* frame() const { return buf_; }
  size_t frameLen() const { return len_; }

 private:
  void discard() { discarding_ = true; escaped_ = false; len_ = 0; }

  uint8_t buf_[kMaxFrame];
  size_t len_ = 0;
  bool inFrame_ = false;
  bool escaped_ = false;
  bool discarding_ = false;
  bool justEmitted_ = false;
  uint32_t lastByteMs_ = 0;
};

}  // namespace wisp
