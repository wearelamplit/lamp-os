#pragma once

#include <cstdint>

namespace lamp {
class Button;
class Touch;

namespace input {

enum class InputType : uint8_t { Button = 0, Touch = 1 };

// A hardware-backed input, ticked once per loop by Lamp before the
// compositor. Concrete drivers read their pin (millis-gated for touch),
// feed the gesture FSM, and fan edge events out through the FSM's callback.
// asButton()/asTouch() give a variant typed access to the underlying FSM
// (poll heldMs, bind callbacks) without RTTI.
class InputSource {
 public:
  virtual ~InputSource() = default;
  virtual void tick(uint32_t nowMs) = 0;
  virtual Button* asButton() { return nullptr; }
  virtual Touch* asTouch() { return nullptr; }
  uint8_t id() const { return id_; }
  InputType type() const { return type_; }

 protected:
  InputSource(uint8_t id, InputType type) : id_(id), type_(type) {}
  uint8_t id_;
  InputType type_;
};

}  // namespace input
}  // namespace lamp
