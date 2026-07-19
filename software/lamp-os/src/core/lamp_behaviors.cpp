// core/lamp_behaviors.cpp: behavior stack wiring.
// initBehaviors assembles the compositor behavior list: configurator (base
// scene), social greetings, expression band, fade-out, knockout, and overlay
// behaviors. Lamp::registerExpressions seeds the expression catalog.
// Called from Lamp::setup() after FrameBuffers are constructed.
//
// Sibling TU of lamp.cpp; shares file-scope state via core/lamp_internal.hpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>

#include "components/apply/apply_brightness.hpp"
#include "behaviors/configurator.hpp"
#include "behaviors/fade_in.hpp"
#include "behaviors/fade_out.hpp"
#include "behaviors/idle.hpp"
#include "behaviors/knockout.hpp"
#include "behaviors/social.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "core/animated_behavior.hpp"
#include "core/behavior_stack_builder.hpp"
#include "core/compositor.hpp"
#include "core/frame_buffer.hpp"
#include "core/override_aggregate.hpp"
#include "core/personality_engine.hpp"
#include "expressions/breathing/breathing_expression.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/glitchy/glitchy_expression.hpp"
#include "expressions/pulse/pulse_expression.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/spotty/spotty_expression.hpp"
#include "util/levels.hpp"

// Bring apply_brightness helpers into file scope so unqualified call sites
// (applyEffectiveBrightness) resolve.
using lamp::applyEffectiveBrightness;

void lamp::Lamp::registerExpressions(lamp::ExpressionRegistry& reg) {
  if (!lamp::any(featuresEnabled(), lamp::Features::DefaultExpressions)) return;
  reg.add(lamp::GlitchyExpression::classDescriptor());
  reg.add(lamp::PulseExpression::classDescriptor());
  reg.add(lamp::BreathingExpression::classDescriptor());
  reg.add(lamp::ShiftyExpression::classDescriptor());
  reg.add(lamp::SpottyExpression::classDescriptor());
}

void initBehaviors(lamp::Features features, lamp::Lamp& self) {
  // Configurators are always needed (they hold saved colors + wisp paint).
  shadeConfiguratorBehavior = lamp::ConfiguratorBehavior(&shade, 120);
  shadeConfiguratorBehavior.colors = shade.defaultColors;
  baseConfiguratorBehavior = lamp::ConfiguratorBehavior(&base, 120);
  baseConfiguratorBehavior.colors = base.defaultColors;

  // ExpressionManager always begins (it owns the expression band in the
  // compositor). Whether it loads saved expressions is gated below.
  expressionManager.begin(&shade, &base);

  self.registerExpressions(expressionManager.registry());

  if (lamp::any(features, lamp::Features::SocialBehavior)) {
    shadeSocialBehavior = lamp::SocialBehavior(&shade, 1200);
    // Live config pointer so SocialBehavior::control reads the current
    // socialMode each tick (user can change personality at runtime; the
    // change rides through settings_blob save + reboot, but the wiring
    // is per-instance regardless).
    shadeSocialBehavior.setConfig(&config);
    shadeSocialBehavior.setMeshLink(&meshLink);
    compositor.behaviorContext().greeting = &shadeSocialBehavior;
    ble_control::setGreetingStateProvider(
        []() { return shadeSocialBehavior.greetingState(); });
    shadeSocialBehavior.setOnGreetingChangeCallback(
        []() { ble_control::notifyStateChange(); });
  }

  if (lamp::any(features, lamp::Features::FadeOutBehavior)) {
    shadeFadeOutBehavior = lamp::FadeOutBehavior(&shade, REBOOT_ANIMATION_FRAMES);
    baseFadeOutBehavior = lamp::FadeOutBehavior(&base, REBOOT_ANIMATION_FRAMES);
  }

  if (lamp::any(features, lamp::Features::KnockoutBehavior)) {
    baseKnockoutBehavior = lamp::KnockoutBehavior(&base, 0, true);
    baseKnockoutBehavior.knockoutPixels = config.base.knockoutPixels;
  }

  // Features::DefaultExpressions gates whether saved NVS expressions are
  // loaded on boot. Subclasses that replace the expression set skip this
  // so their own expressions aren't shadowed by stale NVS data.
  if (lamp::any(features, lamp::Features::DefaultExpressions)) {
    expressionManager.loadFromConfig(config.expressions);
  }

  // Draw order = registration order, last-writer-wins on the surface buffer.
  //
  // Configurator (wisp paint + saved colors) goes FIRST (the base scene).
  // Social greetings overlay next. Expressions come LAST so brief transient
  // effects (glitchy / pulse / breathing / shifty) compose on top and yield
  // when their animation completes (animationState=STOPPED, Compositor skips
  // them, configurator's writes are the final state).
  //
  // The configurator must register BEFORE the expressions so expressions
  // compose ON TOP of whatever the configurator writes. Reversing the order
  // would let the configurator overwrite per-pixel expression writes every
  // frame, making non-exclusive expressions invisible during wisp paint.

  std::vector<lamp::AnimatedBehavior*> allBehaviors = {};

  // Configurator (base scene: saved colors + wisp paint via beginFade)
  allBehaviors.push_back(&baseConfiguratorBehavior);
  allBehaviors.push_back(&shadeConfiguratorBehavior);

  // Social greeting behaviors
  if (lamp::any(features, lamp::Features::SocialBehavior)) {
    allBehaviors.push_back(&shadeSocialBehavior);
  }

  // Expression behaviors LAST: transient effects compose on top. When their
  // animationState transitions to STOPPED the compositor skips them and the
  // configurator's base scene shows through.
  const size_t exprBandStart = allBehaviors.size();
  auto exprBehaviors = expressionManager.getBehaviors();
  allBehaviors.insert(allBehaviors.end(), exprBehaviors.begin(), exprBehaviors.end());

  // Fade-out behaviors run last so reboot animation is on top of everything
  if (lamp::any(features, lamp::Features::FadeOutBehavior)) {
    allBehaviors.push_back(&baseFadeOutBehavior);
    allBehaviors.push_back(&shadeFadeOutBehavior);
  }

  std::vector<lamp::FrameBuffer*> allFbs = {&shade, &base};

  std::vector<lamp::AnimatedBehavior*> underlayBehaviors;
  std::vector<lamp::AnimatedBehavior*> startupBehaviors;
  for (auto* fb : allFbs) {
    underlayBehaviors.push_back(new lamp::IdleBehavior(fb, 0, true));
    startupBehaviors.push_back(new lamp::FadeInBehavior(fb, STARTUP_ANIMATION_FRAMES));
  }
  compositor.begin(allBehaviors, allFbs, underlayBehaviors, startupBehaviors, calculateEffectiveHomeMode());
  // Bound the expression band so runtime-added transients land at its top
  // (addBehavior) and base scenes at its bottom (addBaseBehavior), keeping
  // "all expressions draw together, late in the list" ordering.
  // The end offset accounts for the fade-out behaviors appended after the
  // band (2 when FadeOutBehavior is enabled, 0 otherwise).
  const size_t fadeOutCount =
      lamp::any(features, lamp::Features::FadeOutBehavior) ? 2u : 0u;
  compositor.setExpressionBand(exprBandStart, allBehaviors.size() - fadeOutCount);

  if (lamp::any(features, lamp::Features::KnockoutBehavior)) {
    compositor.overlayBehaviors.push_back(&baseKnockoutBehavior);
  }

  // Finish wiring the shared BehaviorContext. The Compositor self-publishes
  // in its constructor; publishing the ExpressionManager + frame buffer list
  // here lets the expressions just registered by compositor.begin() reach
  // both from this point on. (setCompositor() later in setup() repeats these
  // writes idempotently; they're cheap pointer assignments.)
  auto& behaviorCtx = compositor.behaviorContext();
  behaviorCtx.expressionManager = &expressionManager;
  behaviorCtx.expressionFrameBuffers.clear();
  behaviorCtx.expressionFrameBuffers.push_back(&shade);
  behaviorCtx.expressionFrameBuffers.push_back(&base);
  // Publish the two configurator pointers so the per-surface
  // ColorOverride instances can resolve their target configurator via
  // bind() without grabbing globals.
  behaviorCtx.baseConfigurator = &baseConfiguratorBehavior;
  behaviorCtx.shadeConfigurator = &shadeConfiguratorBehavior;
  // Mesh + identity surface for custom behaviors
  behaviorCtx.lampRoster = &lamp::lampRoster;
  // bind() the override instances. From here on apply()/restore() will
  // drive the right configurator's beginFade.
  lamp::overrides.base.bind(behaviorCtx, lamp_protocol::OverrideSurface::Base);
  lamp::overrides.shade.bind(behaviorCtx, lamp_protocol::OverrideSurface::Shade);
  // Wisp-state change callbacks. Each surface's ColorOverride fires
  // when wisp goes from un-controlling → controlling or vice versa
  // (edge-triggered inside maybeNotifyWispStateChange). The Flutter
  // app subscribes to CHAR_WISP_STATUS so a notify lands the moment
  // a surface transitions; the indicator widget pops on / off without
  // having to poll.
  lamp::overrides.base.setOnWispStateChangeCallback(
      []() { ble_control::notifyWispStatus(); });
  lamp::overrides.shade.setOnWispStateChangeCallback(
      []() { ble_control::notifyWispStatus(); });
  // Provider that the CHAR_WISP_STATUS read merges into the JSON. Lives
  // here so the ColorOverride globals stay out of the network layer.
  lamp::lampRoster.setLampWispStateProvider([]() {
    lamp::LampRoster::LampWispState ws;
    ws.controllingBase  = lamp::overrides.base.isWispActive();
    ws.controllingShade = lamp::overrides.shade.isWispActive();
    if (lamp::overrides.base.hasLastWispColor()) {
      ws.baseWispColor = lamp::colorToHexString(
          lamp::overrides.base.lastWispColor());
    }
    if (lamp::overrides.shade.hasLastWispColor()) {
      ws.shadeWispColor = lamp::colorToHexString(
          lamp::overrides.shade.lastWispColor());
    }
#ifdef LAMP_DEBUG
    Serial.printf("[wisp_state] provider isWispActive base=%d shade=%d hasBaseC=%d hasShadeC=%d\n",
                  ws.controllingBase ? 1 : 0,
                  ws.controllingShade ? 1 : 0,
                  lamp::overrides.base.hasLastWispColor() ? 1 : 0,
                  lamp::overrides.shade.hasLastWispColor() ? 1 : 0);
#endif
    return ws;
  });
  // BrightnessOverride routes its change-driven callback into the
  // existing applyEffectiveBrightness path so master-brightness fades
  // share the same NeoPixel setBrightness entry point.
  lamp::overrides.brightness.setOnChangeCallback([]() { applyEffectiveBrightness(); });

  lamp::personalityEngine.begin(&config);
}
