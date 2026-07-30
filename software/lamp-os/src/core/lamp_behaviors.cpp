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
#include "expressions/flicker/flicker_expression.hpp"
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
  reg.add(lamp::FlickerExpression::classDescriptor());
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
  // Configurator (wisp paint + saved colors) goes FIRST (the base scene), so
  // expressions compose ON TOP of whatever it writes. Reversing that order
  // would let the configurator overwrite per-pixel expression writes every
  // frame, making non-exclusive expressions invisible during wisp paint.
  //
  // Expressions compose next. Brief transient effects (glitchy / pulse /
  // breathing / shifty) yield when their animation completes
  // (animationState=STOPPED, the Compositor skips them and the configurator's
  // writes are the final state).
  //
  // The social greeting composites ON TOP of the expressions: its draw() eases
  // in over whatever is running and eases back out toward the live surface, so
  // a continuous expression (flicker) can't bury it. Fade-out behaviors stay
  // last so a reboot animation sits on top of even the greeting.

  std::vector<lamp::AnimatedBehavior*> allBehaviors = {};

  // Configurator (base scene: saved colors + wisp paint via beginFade)
  allBehaviors.push_back(&baseConfiguratorBehavior);
  allBehaviors.push_back(&shadeConfiguratorBehavior);

  // Expression band: transient effects compose over the base scene.
  const size_t exprBandStart = allBehaviors.size();
  auto exprBehaviors = expressionManager.getBehaviors();
  allBehaviors.insert(allBehaviors.end(), exprBehaviors.begin(), exprBehaviors.end());
  const size_t exprBandEnd = allBehaviors.size();

  // Social greeting on top of the expressions so it fades in over whatever is
  // running and eases back out to the live expression.
  if (lamp::any(features, lamp::Features::SocialBehavior)) {
    allBehaviors.push_back(&shadeSocialBehavior);
  }

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
  // "all expressions draw together" ordering. The band excludes the social
  // greeting and the fade-out behaviors that follow it, so a runtime-added
  // expression inserts before the greeting and the greeting stays on top.
  compositor.setExpressionBand(exprBandStart, exprBandEnd);

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
  behaviorCtx.meshLink = &meshLink;
  // bind() the override instances. From here on apply()/restore() will
  // drive the right configurator's beginFade.
  lamp::overrides.base.bind(behaviorCtx, lamp_protocol::OverrideSurface::Base);
  lamp::overrides.shade.bind(behaviorCtx, lamp_protocol::OverrideSurface::Shade);
  // Provider that the CHAR_WISP_STATUS read merges into the JSON. Wisp control
  // is driven by MSG_WISP_STATE, so both surfaces share the compositor's
  // STATE-derived active flag + colors. drainWispState pushes the notify.
  lamp::lampRoster.setLampWispStateProvider([]() {
    lamp::LampRoster::LampWispState ws;
    const bool active = compositor.wispActive();
    ws.controllingBase  = active;
    ws.controllingShade = active;
    if (active) {
      ws.baseWispColor  = lamp::colorToHexString(compositor.wispStateBaseColor());
      ws.shadeWispColor = lamp::colorToHexString(compositor.wispStateShadeColor());
    }
#ifdef LAMP_DEBUG
    Serial.printf("[wisp_state] provider active=%d\n", active ? 1 : 0);
#endif
    return ws;
  });
  // BrightnessOverride routes its change-driven callback into the
  // existing applyEffectiveBrightness path so master-brightness fades
  // share the same NeoPixel setBrightness entry point.
  lamp::overrides.brightness.setOnChangeCallback([]() { applyEffectiveBrightness(); });

  lamp::personalityEngine.begin(&config);
}
