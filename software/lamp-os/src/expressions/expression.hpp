#ifndef LAMP_EXPRESSIONS_EXPRESSION_H
#define LAMP_EXPRESSIONS_EXPRESSION_H

#include <cstdint>
#include <map>
#include <random>
#include <variant>
#include <vector>

#include "../core/animated_behavior.hpp"
#include "../util/color.hpp"

namespace lamp {

// Forward declaration
class Compositor;

// Set global compositor for expressions to check exclusive state
void setGlobalCompositor(Compositor* compositor);

enum ExpressionTarget {
  TARGET_SHADE = 1,
  TARGET_BASE = 2,
  TARGET_BOTH = 3
};

/**
 * @brief Base class for lamp expressions - behaviors that add personality
 * Expressions are time-triggered behaviors that modify the lamp's appearance
 */
class Expression : public AnimatedBehavior {
 protected:
  std::vector<Color> savedBuffer;
  std::vector<Color> colors;
  uint32_t nextTriggerMs = 0;
  uint32_t intervalMinMs = 60000;   // 1 min default
  uint32_t intervalMaxMs = 900000;  // 15 min default
  uint32_t lastCompletedLoop = 0;   // Track last completed animation loop
  ExpressionTarget target = TARGET_BOTH;
  std::mt19937 rng{esp_random()};

  /**
   * @brief Schedule next trigger within configured interval range
   */
  void scheduleNextTrigger();

  /**
   * @brief Save current buffer state for restoration
   */
  void saveBufferState();

  /**
   * @brief Check if this expression should affect current buffer
   */
  bool shouldAffectBuffer();

  /**
   * @brief Check if this expression should pause for an exclusive behavior
   */
  bool shouldPause() const;

 public:
  using AnimatedBehavior::AnimatedBehavior;

  /**
   * @brief Configure expression parameters (initial setup)
   * @param inColors Color palette for the expression
   * @param inIntervalMin Minimum trigger interval in seconds
   * @param inIntervalMax Maximum trigger interval in seconds
   * @param inTarget Which lamp component to affect
   */
  void configure(const std::vector<Color>& inColors,
                 uint32_t inIntervalMin,
                 uint32_t inIntervalMax,
                 ExpressionTarget inTarget);

  void control() override;

  /**
   * @brief Manually trigger this expression to start immediately
   * Can be called from UI or other expressions
   */
  void trigger();

  /**
   * @brief Get random color from configured palette
   */
  Color getRandomColor();

  /**
   * @brief Generic parameter extraction utilities
   */
  uint32_t extractUint32Parameter(const std::map<std::string, uint32_t>& parameters,
                                   const std::string& key, uint32_t defaultValue) const;

protected:
  /**
   * @brief Expression-specific setup when triggered (REQUIRED)
   * Called when expression starts (both manual and automatic triggers)
   * Implement this to set up colors, state, etc.
   */
  virtual void onTrigger() = 0;

  /**
   * @brief Per-frame update during animation (OPTIONAL)
   * Called every frame while animationState == PLAYING
   * Implement this for continuous effects like moving waves
   */
  virtual void onUpdate() { }

  /**
   * @brief Cleanup when animation completes (OPTIONAL)
   * Called when animation finishes
   * Implement this for state cleanup or chaining effects
   */
  virtual void onComplete() { }
};

}  // namespace lamp

#endif