#include "osc/slip_decoder.hpp"

#include <lampos/clock.hpp>

namespace wisp {

void SlipDecoder::retire(uint32_t nowMs) { lastByteMs_ = lampos::retireStamp(nowMs, lastByteMs_); }

SlipDecoder::Result SlipDecoder::feed(uint8_t b, uint32_t nowMs) {
  if (justEmitted_) {
    justEmitted_ = false;
    len_ = 0;
  }
  if (inFrame_ && nowMs - lastByteMs_ >= kIdleResetMs) {
    inFrame_ = false;
    escaped_ = false;
    discarding_ = false;
    len_ = 0;
  }
  lastByteMs_ = nowMs;
  if (!inFrame_) {
    if (b != kEnd) return Result::Text;
    inFrame_ = true;
    escaped_ = false;
    discarding_ = false;
    len_ = 0;
    return Result::Consumed;
  }
  if (b == kEnd) {
    if (discarding_ || escaped_) {
      inFrame_ = false;
      discarding_ = false;
      escaped_ = false;
      len_ = 0;
      return Result::Consumed;
    }
    if (len_ == 0) return Result::Consumed;
    inFrame_ = false;
    justEmitted_ = true;
    return Result::Frame;
  }
  if (discarding_) return Result::Consumed;
  if (escaped_) {
    escaped_ = false;
    if (b == kEscEnd) {
      b = kEnd;
    } else if (b == kEscEsc) {
      b = kEsc;
    } else {
      discard();
      return Result::Consumed;
    }
  } else if (b == kEsc) {
    escaped_ = true;
    return Result::Consumed;
  }
  if (len_ >= kMaxFrame) {
    discard();
    return Result::Consumed;
  }
  buf_[len_++] = b;
  return Result::Consumed;
}

}  // namespace wisp
