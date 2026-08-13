#include "expression_observer.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

namespace lamp {

void ExpressionObserverRegistry::registerObserver(IExpressionObserver* o) {
  if (!o) return;
  for (size_t i = 0; i < count_; ++i) {
    if (observers_[i] == o) return;
  }
  if (count_ >= MAX_OBSERVERS) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    Serial.println("[expr] observer registry full");
#endif
    return;
  }
  observers_[count_++] = o;
}

void ExpressionObserverRegistry::unregisterObserver(IExpressionObserver* o) {
  for (size_t i = 0; i < count_; ++i) {
    if (observers_[i] == o) {
      observers_[i] = observers_[--count_];
      observers_[count_] = nullptr;
      return;
    }
  }
}

void ExpressionObserverRegistry::fanOut(const uint8_t sourceMac[6],
                                        const ExpressionInvocation& inv) {
  // Snapshot before iteration: an unregisterObserver(self) inside a callback
  // swap-erases into the live array; iterating a copy avoids skipping the
  // element that was moved into the vacated slot.
  IExpressionObserver* snap[MAX_OBSERVERS];
  const size_t n = count_;
  for (size_t i = 0; i < n; ++i) snap[i] = observers_[i];
  for (size_t i = 0; i < n; ++i) snap[i]->onPeerExpression(sourceMac, inv);
}

}  // namespace lamp
