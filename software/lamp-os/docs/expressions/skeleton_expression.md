# Skeleton Expression Example

This is a minimal example of creating a new expression.

## Header File (skeleton_expression.hpp)

```cpp
#ifndef LAMP_EXPRESSIONS_SKELETON_H
#define LAMP_EXPRESSIONS_SKELETON_H

#include "./expression.hpp"

namespace lamp {

class SkeletonExpression : public Expression {
private:
  // Your state variables
  Color activeColor;
  float intensity = 1.0f;

public:
  using Expression::Expression;

  // Constructor
  SkeletonExpression(FrameBuffer* inBuffer, uint32_t inFrames = 30)
      : Expression(inBuffer, inFrames) {
    isExclusive = false;         // Can blend with other expressions
  }

  // Configuration (add custom parameters as needed)
  void configure(const std::vector<Color>& inColors,
                 uint32_t inIntervalMin,
                 uint32_t inIntervalMax,
                 ExpressionTarget inTarget,
                 float inIntensity = 1.0f) {
    // Call base configuration
    Expression::configure(inColors, inIntervalMin, inIntervalMax, inTarget);

    // Your custom configuration
    intensity = inIntensity;
  }

  void draw() override;

protected:
  void onTrigger() override;
  void onUpdate() override;
  void onComplete() override;
};

}  // namespace lamp

#endif
```

## Implementation File (skeleton_expression.cpp)

```cpp
#include "./skeleton_expression.hpp"

namespace lamp {

void SkeletonExpression::onTrigger() {
  // Called when expression starts
  // Set up your initial state
  activeColor = getRandomColor();  // Pick random color from palette
  frame = 0;                        // Reset animation frame
}

void SkeletonExpression::onUpdate() {
  // Called every frame while animating
  // Update any dynamic state
  // This is optional - only implement if needed
}

void SkeletonExpression::onComplete() {
  // Called when animation finishes
  // Clean up or trigger other expressions
  // This is optional - only implement if needed
}

void SkeletonExpression::draw() {
  // Pause if an exclusive behavior is running
  if (shouldPause()) return;

  // Only draw to appropriate buffer
  if (!shouldAffectBuffer()) {
    nextFrame();
    return;
  }

  // Calculate animation progress (0.0 to 1.0)
  float progress = static_cast<float>(frame) / frames;

  // Apply your visual effect
  for (int i = 0; i < fb->pixelCount; i++) {
    // Example: Fade in the color
    float fadeAmount = progress * intensity;
    fb->buffer[i] = fb->buffer[i].lerp(activeColor, fadeAmount);
  }

  // Advance to next frame
  nextFrame();
}

}  // namespace lamp
```

## Adding to Expression Manager

In `expression_manager.cpp`, add your expression type:

```cpp
auto createExpression = [&](FrameBuffer* buffer) -> std::unique_ptr<Expression> {
  // ... existing expressions ...

  if (config.type == "skeleton") {
    auto expr = std::make_unique<SkeletonExpression>(buffer, config.duration);
    expr->configure(config.colors, config.intervalMin, config.intervalMax,
                    target, config.intensity);
    return expr;
  }

  return nullptr;
};
```

## Configuration

Add to your expression config:

```json
{
  "type": "skeleton",
  "enabled": true,
  "colors": [
    {"r": 255, "g": 0, "b": 100, "w": 0}
  ],
  "intervalMin": 30,
  "intervalMax": 60,
  "target": 3,
  "duration": 30,
  "intensity": 0.5
}
```

## Key Points

1. **Always check `shouldPause()`** - Respects exclusive expressions
2. **Always check `shouldAffectBuffer()`** - Respects target configuration
3. **Call `nextFrame()`** - Advances animation and handles completion
4. **Use `getRandomColor()`** - Picks from configured palette
5. **Handle re-triggers** - Expression can be triggered anytime

That's it! This skeleton provides a complete, working expression that you can customize for any effect.