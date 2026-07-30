// Native-host tests for the shared social-openness ladder (core/social_tier.hpp).
//
// socialTier is the single source for the disposition×mode openness ordering;
// personality_engine's profileFor derives its greeting profile from the tier
// (profileForTier, 1:1 per rung). Pinning the tier table here is therefore the
// behavioral truth for the greeting mapping too — no separate profileFor mirror.
//
// Pins:
//   1. socialTier(disposition, mode) against the ladder table, all 15 cells.
//   2. socialBidResponse(disposition, mode) against the accept table, 15 cells.

#include <unity.h>

#include <cstdint>

#include "core/social_tier.hpp"

using lamp::SocialMode;
using lamp::socialTier;
using lamp::socialBidResponse;

void setUp(void) {}
void tearDown(void) {}

static const SocialMode kModes[3] = {
    SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert};

// --- 1. socialTier 15-cell pin ---

void test_social_tier_15_cells() {
  // [disposition-1][mode]  (disposition 1..5, mode I/A/E)
  const uint8_t expect[5][3] = {
      {0, 0, 0},  // Salty   → Snub
      {1, 1, 1},  // Wary    → PartialSnub
      {2, 3, 3},  // Neutral → Minimal / Standard / Standard (E clamped)
      {4, 5, 6},  // Fond    → Gentle / Warm / Enthused
      {5, 6, 7},  // Smitten → Warm / Enthused / Effusive
  };
  for (uint8_t disp = 1; disp <= 5; ++disp) {
    for (int m = 0; m < 3; ++m) {
      TEST_ASSERT_EQUAL_UINT8(expect[disp - 1][m], socialTier(disp, kModes[m]));
    }
  }
}

void test_unknown_disposition_maps_neutral() {
  for (int m = 0; m < 3; ++m) {
    TEST_ASSERT_EQUAL_UINT8(socialTier(3, kModes[m]), socialTier(0, kModes[m]));
    TEST_ASSERT_EQUAL_UINT8(socialTier(3, kModes[m]), socialTier(99, kModes[m]));
  }
}

// --- 2. socialBidResponse 15-cell pin ---

void test_social_bid_response_15_cells() {
  const uint8_t expect[5][3] = {
      {0,   0,   0},    // Salty
      {0,   0,   0},    // Wary
      {15,  30,  30},   // Neutral
      {45,  60,  80},   // Fond
      {60,  80,  100},  // Smitten
  };
  for (uint8_t disp = 1; disp <= 5; ++disp) {
    for (int m = 0; m < 3; ++m) {
      TEST_ASSERT_EQUAL_UINT8(expect[disp - 1][m],
                              socialBidResponse(disp, kModes[m]));
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_social_tier_15_cells);
  RUN_TEST(test_unknown_disposition_maps_neutral);
  RUN_TEST(test_social_bid_response_15_cells);
  return UNITY_END();
}
