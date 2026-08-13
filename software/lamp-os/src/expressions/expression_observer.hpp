#pragma once

#include <cstdint>

#include "expressions/expression_invocation.hpp"

namespace lamp {

// Observer interface for MSG_EVENT peer-expression announces. Behaviors
// register here to react when a nearby lamp fires an expression.
class IExpressionObserver {
 public:
  virtual void onPeerExpression(const uint8_t sourceMac[6],
                                const ExpressionInvocation& inv) = 0;
  virtual ~IExpressionObserver() = default;
};

// Bounded fixed-capacity fan-out registry. No dynamic alloc; overflow is
// logged and the excess registration is dropped.
class ExpressionObserverRegistry {
 public:
  static constexpr size_t MAX_OBSERVERS = 4;

  // Caller must call unregisterObserver(this) before destroying the observer;
  // the registry holds a raw pointer and has no way to detect stale entries.
  void registerObserver(IExpressionObserver* o);
  void unregisterObserver(IExpressionObserver* o);
  void fanOut(const uint8_t sourceMac[6], const ExpressionInvocation& inv);

 private:
  IExpressionObserver* observers_[MAX_OBSERVERS] = {};
  size_t count_ = 0;
};

}  // namespace lamp

// Single global instance, defined in core/lamp.cpp.
extern lamp::ExpressionObserverRegistry expressionObserverRegistry;
