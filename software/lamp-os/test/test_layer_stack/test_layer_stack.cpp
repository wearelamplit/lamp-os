#include <unity.h>

#include "render/layer_stack.hpp"
#include "util/color.hpp"
#include "util/fade.hpp"

using lamp::Color;
using lamp::EasedScalar;
using lamp::LayerStack;
using lamp::mixColorWeight;

void setUp() {}
void tearDown() {}

// Drive one stack's wisp color to its target under a fixed presence w.
static void settle(LayerStack& s, float w, int frames = 60) {
  for (int i = 0; i < frames; ++i) s.tick(w);
}

static void assertColorEq(Color expected, Color actual) {
  TEST_ASSERT_EQUAL_UINT8(expected.r, actual.r);
  TEST_ASSERT_EQUAL_UINT8(expected.g, actual.g);
  TEST_ASSERT_EQUAL_UINT8(expected.b, actual.b);
  TEST_ASSERT_EQUAL_UINT8(expected.w, actual.w);
}

void test_empty_stack_own_only() {
  LayerStack s;
  const Color own(120, 40, 200, 10);
  s.tick(0.0f);
  assertColorEq(own, s.composite(own));
}

void test_wisp_opaque_covers_own() {
  LayerStack s;
  const Color own(10, 20, 30, 40);
  const Color wisp(200, 150, 100, 5);
  s.setWispTarget(wisp);
  settle(s, 1.0f);
  assertColorEq(wisp, s.composite(own));
}

void test_wisp_zero_shows_own() {
  LayerStack s;
  const Color own(10, 20, 30, 40);
  s.setWispTarget(Color(255, 0, 0, 0));
  settle(s, 0.0f);
  assertColorEq(own, s.composite(own));
}

void test_wisp_half_blends() {
  LayerStack s;
  const Color own(0, 0, 0, 0);
  const Color wisp(200, 100, 40, 8);
  s.setWispTarget(wisp);
  settle(s, 0.5f);
  const Color expected = mixColorWeight(own, wisp, 0.5f);
  assertColorEq(expected, s.composite(own));
  // Half-blend of {0..} with {200,100,40,8} is the integer half.
  assertColorEq(Color(100, 50, 20, 4), s.composite(own));
}

void test_expression_over_wisp() {
  LayerStack s;
  const Color own(0, 0, 0, 0);
  const Color wisp(200, 100, 40, 0);
  const Color exprColor(0, 0, 250, 0);
  s.setWispTarget(wisp);
  // wispDimFloor 1.0 => opacity == userOpacity (0.3) regardless of w.
  s.setExpressionLayer(0, exprColor, /*enabled=*/true, /*userOpacity=*/0.3f,
                       /*wispDimFloor=*/1.0f);
  settle(s, 1.0f, 120);
  const Color base = mixColorWeight(own, wisp, 1.0f);  // == wisp
  const Color expected = mixColorWeight(base, exprColor, 0.3f);
  assertColorEq(expected, s.composite(own));
}

void test_shared_w_drives_both_surfaces() {
  LayerStack base;
  LayerStack shade;
  const Color baseOwn(10, 10, 10, 0);
  const Color shadeOwn(20, 20, 20, 0);
  const Color baseWisp(200, 0, 0, 0);
  const Color shadeWisp(0, 200, 0, 0);
  base.setWispTarget(baseWisp);
  shade.setWispTarget(shadeWisp);

  EasedScalar w(0.1f, 0.0f);
  w.setTarget(1.0f);
  for (int i = 0; i < 60; ++i) {
    w.tick();
    base.tick(w.value());
    shade.tick(w.value());
  }
  // One w at 1.0 fully reveals BOTH surfaces' wisp colors.
  assertColorEq(baseWisp, base.composite(baseOwn));
  assertColorEq(shadeWisp, shade.composite(shadeOwn));

  w.setTarget(0.0f);
  for (int i = 0; i < 60; ++i) {
    w.tick();
    base.tick(w.value());
    shade.tick(w.value());
  }
  // The same w at 0.0 hides BOTH wisp layers.
  assertColorEq(baseOwn, base.composite(baseOwn));
  assertColorEq(shadeOwn, shade.composite(shadeOwn));
}

void test_wisp_retarget_no_jump() {
  LayerStack s;
  s.setWispTarget(Color(255, 255, 255, 255));
  for (int i = 0; i < 10; ++i) s.tick(1.0f);  // partway toward white
  const Color mid = s.wisp().value();
  // Retarget mid-ease: no jump at the retarget instant, then smooth to the new target.
  s.setWispTarget(Color(0, 0, 255, 0));
  assertColorEq(mid, s.wisp().value());
  settle(s, 1.0f);
  assertColorEq(Color(0, 0, 255, 0), s.wisp().value());
}

void test_clear_expression_layer() {
  LayerStack s;
  const Color own(0, 0, 0, 0);
  const Color exprColor(0, 0, 250, 0);
  s.setExpressionLayer(0, exprColor, /*enabled=*/true, /*userOpacity=*/1.0f,
                       /*wispDimFloor=*/1.0f);
  settle(s, 0.0f);
  assertColorEq(exprColor, s.composite(own));  // full expression, no wisp
  s.clearExpressionLayer(0);
  assertColorEq(own, s.composite(own));  // gone
}

void test_wisp_dim_floor_dims_expression() {
  LayerStack s;
  const Color own(0, 0, 0, 0);
  const Color wisp(200, 0, 0, 0);
  const Color exprColor(0, 0, 250, 0);
  s.setWispTarget(wisp);
  s.setExpressionLayer(0, exprColor, /*enabled=*/true, /*userOpacity=*/1.0f,
                       /*wispDimFloor=*/0.3f);
  // w=1: the expression is dimmed to 0.3 over the wisp.
  settle(s, 1.0f, 120);
  const Color dimmed =
      mixColorWeight(mixColorWeight(own, wisp, 1.0f), exprColor, 0.3f);
  assertColorEq(dimmed, s.composite(own));
  // w=0: the same expression is back at full userOpacity over own.
  settle(s, 0.0f, 120);
  assertColorEq(exprColor, s.composite(own));
}

// Mirror of compositor.cpp's compositeWisp: per-surface presence eased toward 1
// while that surface is held, toward 0 when released.
struct WispRig {
  LayerStack base;
  LayerStack shade;
  EasedScalar basePresence{0.1f, 0.0f};
  EasedScalar shadePresence{0.1f, 0.0f};

  void overrideBase(Color c) { base.setWispTarget(c); }
  void overrideShade(Color c) { shade.setWispTarget(c); }

  void tick(bool baseHeld, bool shadeHeld) {
    basePresence.setTarget(baseHeld ? 1.0f : 0.0f);
    shadePresence.setTarget(shadeHeld ? 1.0f : 0.0f);
    basePresence.tick();
    shadePresence.tick();
    base.tick(basePresence.value());
    shade.tick(shadePresence.value());
  }
};

void test_adapter_override_reveals_then_restore_homes() {
  WispRig rig;
  const Color baseOwn(10, 10, 10, 0);
  const Color shadeOwn(20, 20, 20, 0);
  const Color baseWisp(200, 0, 0, 0);
  const Color shadeWisp(0, 200, 0, 0);

  // Override event: set targets, then hold (presence -> 1).
  rig.overrideBase(baseWisp);
  rig.overrideShade(shadeWisp);
  for (int i = 0; i < 60; ++i) rig.tick(/*baseHeld=*/true, /*shadeHeld=*/true);
  assertColorEq(baseWisp, rig.base.composite(baseOwn));
  assertColorEq(shadeWisp, rig.shade.composite(shadeOwn));

  // Restore event: release (presence -> 0) eases both home to own.
  for (int i = 0; i < 60; ++i) rig.tick(/*baseHeld=*/false, /*shadeHeld=*/false);
  assertColorEq(baseOwn, rig.base.composite(baseOwn));
  assertColorEq(shadeOwn, rig.shade.composite(shadeOwn));
}

void test_adapter_per_surface_presence_no_stale_sibling() {
  WispRig rig;
  const Color baseOwn(10, 10, 10, 0);
  const Color shadeOwn(20, 20, 20, 0);
  const Color baseWisp(200, 0, 0, 0);
  const Color shadeWisp(0, 200, 0, 0);
  rig.overrideBase(baseWisp);
  rig.overrideShade(shadeWisp);
  for (int i = 0; i < 60; ++i) rig.tick(/*baseHeld=*/true, /*shadeHeld=*/true);
  assertColorEq(baseWisp, rig.base.composite(baseOwn));
  assertColorEq(shadeWisp, rig.shade.composite(shadeOwn));

  // Base restores while shade keeps holding: base eases home to its own color,
  // never revealing the sibling's stale target; shade stays revealed.
  for (int i = 0; i < 60; ++i) rig.tick(/*baseHeld=*/false, /*shadeHeld=*/true);
  assertColorEq(baseOwn, rig.base.composite(baseOwn));
  assertColorEq(shadeWisp, rig.shade.composite(shadeOwn));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_stack_own_only);
  RUN_TEST(test_wisp_opaque_covers_own);
  RUN_TEST(test_wisp_zero_shows_own);
  RUN_TEST(test_wisp_half_blends);
  RUN_TEST(test_expression_over_wisp);
  RUN_TEST(test_shared_w_drives_both_surfaces);
  RUN_TEST(test_wisp_retarget_no_jump);
  RUN_TEST(test_clear_expression_layer);
  RUN_TEST(test_wisp_dim_floor_dims_expression);
  RUN_TEST(test_adapter_override_reveals_then_restore_homes);
  RUN_TEST(test_adapter_per_surface_presence_no_stale_sibling);
  return UNITY_END();
}
