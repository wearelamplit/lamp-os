#ifndef LAMP_EXPRESSIONS_MANAGER_H
#define LAMP_EXPRESSIONS_MANAGER_H

#include <memory>
#include <vector>

#include "../config/config_types.hpp"
#include "../core/frame_buffer.hpp"
#include "./expression.hpp"
#include "./glitchy_expression.hpp"
#include "./shifty_expression.hpp"
#include "./pulse_expression.hpp"
#include "./breathing_expression.hpp"

namespace lamp {

// Forward declaration
class ExpressionManager;

// Global expression manager access
void setGlobalExpressionManager(ExpressionManager* manager);
ExpressionManager* getGlobalExpressionManager();

/**
 * @brief Manages active expressions and their lifecycle
 */
class ExpressionManager {
 private:
  // Store expression with its type for triggering
  struct ExpressionEntry {
    std::unique_ptr<Expression> expression;
    std::string type;
  };
  std::vector<ExpressionEntry> expressions;
  FrameBuffer* shadeBuffer = nullptr;
  FrameBuffer* baseBuffer = nullptr;

 public:
  /**
   * @brief Initialize manager with frame buffers
   */
  void begin(FrameBuffer* shade, FrameBuffer* base);

  /**
   * @brief Load expressions from config
   */
  void loadFromConfig(const ExpressionSettings& settings);

  /**
   * @brief Get active expression behaviors for compositor
   */
  std::vector<AnimatedBehavior*> getBehaviors();

  /**
   * @brief Add a new expression
   */
  void addExpression(const ExpressionConfig& config);

  /**
   * @brief Remove expression at index
   */
  void removeExpression(size_t index);

  /**
   * @brief Clear all expressions
   */
  void clear();

  /**
   * @brief Trigger an expression by type
   * @param type Expression type to trigger (e.g., "glitchy", "shifty", "pulse")
   * @return true if expression was found and triggered, false otherwise
   */
  bool triggerExpression(const std::string& type);
};

}  // namespace lamp

#endif