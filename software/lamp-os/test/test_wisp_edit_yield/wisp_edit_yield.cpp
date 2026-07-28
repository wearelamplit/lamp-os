// Native-host test for the compositor's per-surface wisp-presence gating.
// The real Compositor::compositeWisp reaches through Arduino/millis and a
// LayerStack, so following the test_wisp_presence_edge convention the target
// decision is mirrored inline against the real EasedScalar the compositor uses.
//
// Editing one surface must fade only that surface's wisp layer to 0 (exposing
// the operator's live edit) while the other keeps its wisp paint.

#include <unity.h>

#include "core/override_aggregate.hpp"
#include "util/eased_scalar.hpp"

using lamp::EasedScalar;
using lamp::OverrideAggregate;

namespace {

// Mirror of compositeWisp's presence-target computation.
void setTargets(bool active, bool baseEditing, bool shadeEditing,
                EasedScalar& base, EasedScalar& shade) {
  const bool baseHeld = active && !baseEditing;
  const bool shadeHeld = active && !shadeEditing;
  base.setTarget(baseHeld ? 1.0f : 0.0f);
  shade.setTarget(shadeHeld ? 1.0f : 0.0f);
}

void settle(EasedScalar& s) {
  for (int i = 0; i < 60; ++i) s.tick();
}

}  // namespace

void setUp() {}
void tearDown() {}

// Wisp active, base editing: base fades to 0, shade holds at 1.
void test_editing_base_fades_only_base() {
  EasedScalar base(0.1f), shade(0.1f);
  setTargets(true, false, false, base, shade);
  settle(base);
  settle(shade);

  setTargets(true, /*baseEditing=*/true, /*shadeEditing=*/false, base, shade);
  settle(base);
  settle(shade);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, base.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, shade.value());
}

// Symmetric: editing the shade fades only the shade.
void test_editing_shade_fades_only_shade() {
  EasedScalar base(0.1f), shade(0.1f);
  setTargets(true, false, false, base, shade);
  settle(base);
  settle(shade);

  setTargets(true, /*baseEditing=*/false, /*shadeEditing=*/true, base, shade);
  settle(base);
  settle(shade);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, base.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, shade.value());
}

// Both editing fades both.
void test_editing_both_fades_both() {
  EasedScalar base(0.1f), shade(0.1f);
  setTargets(true, true, true, base, shade);
  settle(base);
  settle(shade);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, base.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, shade.value());
}

// Wisp inactive: editing state is irrelevant, both stay 0.
void test_inactive_ignores_editing() {
  EasedScalar base(0.1f), shade(0.1f);
  setTargets(false, false, false, base, shade);
  settle(base);
  settle(shade);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, base.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, shade.value());
}

// The zone preview yields a surface through OverrideAggregate::setSurfacePreview
// (base=0x01, shade=0x02); the compositor reads operatorEditing() to fade the
// wisp off it. Preview one surface, the other keeps its paint; clearing restores.
void test_surface_preview_yields_only_target() {
  OverrideAggregate ov;

  ov.setSurfacePreview(0x01, true);  // base only, as TARGET_BASE zone preview
  TEST_ASSERT_TRUE(ov.base.operatorEditing());
  TEST_ASSERT_FALSE(ov.shade.operatorEditing());

  EasedScalar base(0.1f), shade(0.1f);
  setTargets(true, ov.base.operatorEditing(), ov.shade.operatorEditing(), base, shade);
  settle(base);
  settle(shade);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, base.value());   // wisp yields base
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, shade.value());  // shade keeps paint

  ov.setSurfacePreview(0x01, false);  // clearZonePreview releases it
  TEST_ASSERT_FALSE(ov.base.operatorEditing());
  setTargets(true, ov.base.operatorEditing(), ov.shade.operatorEditing(), base, shade);
  settle(base);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, base.value());
}

// TARGET_BOTH yields both surfaces.
void test_surface_preview_both() {
  OverrideAggregate ov;
  ov.setSurfacePreview(0x03, true);
  TEST_ASSERT_TRUE(ov.base.operatorEditing());
  TEST_ASSERT_TRUE(ov.shade.operatorEditing());
  ov.setSurfacePreview(0x03, false);
  TEST_ASSERT_FALSE(ov.base.operatorEditing());
  TEST_ASSERT_FALSE(ov.shade.operatorEditing());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_editing_base_fades_only_base);
  RUN_TEST(test_editing_shade_fades_only_shade);
  RUN_TEST(test_editing_both_fades_both);
  RUN_TEST(test_inactive_ignores_editing);
  RUN_TEST(test_surface_preview_yields_only_target);
  RUN_TEST(test_surface_preview_both);
  return UNITY_END();
}
