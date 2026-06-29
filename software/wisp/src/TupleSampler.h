// TupleSampler — pure function from (palette, mac) → two colors.
//
// When wisp paints the mesh, every peer gets two colors sampled at
// MAC-hashed positions along the active Aurora gradient. Same MAC always
// produces the same pair, so the lamp's two surfaces (base + shade) stay
// stable across repeated paints until either the palette changes or the
// lamp's MAC changes (i.e. never, during a session).
//
// Host-portable: no Arduino, no FreeRTOS. The .cpp is included in the native
// build via build_src_filter, and the test fixture redeclares the algorithm
// locally for full isolation.
//
// Algorithm: pick TWO authored palette colors per lamp, with the (base,
// shade) assignment also randomized per-MAC. No gradient interpolation —
// each surface gets one of the operator's actual authored stops, so the
// fleet reads as "two colors from anywhere in the palette" rather than
// "smooth blends biased toward one endpoint" (the prior interpolation
// approach made every lamp visually similar to its neighbours on a
// 2-stop palette — pure mid-tones, never pure endpoints).
//
//   1. Dedupe adjacent near-identical authored stops; no padding.
//   2. idxA = fnv1a(mac) % N, idxB = fnv1a(mac XOR kGolden) % N.
//   3. If idxA == idxB and N >= 2: idxB = (idxB + 1) % N (deterministic
//      per-MAC bump so we don't paint the same color on both surfaces
//      when there are options).
//   4. swap = hash(mac XOR kSwapSalt) & 1.
//   5. base  = swap ? palette[idxB] : palette[idxA];
//      shade = swap ? palette[idxA] : palette[idxB].
//      ~50% of MACs end up with the swap branch, breaking the prior
//      "always (idxA → base, idxB → shade)" pattern that visually clamped
//      the fleet to e.g. "all blue tops, red bottoms".
//
// `hash(mac, salt)` is FNV-1a over salt-XORed bytes followed by a Stafford
// variant-13 finalizer (see TupleSampler.cpp). The finalizer is what makes
// the swap bit + the modulo-N indices well-distributed for fleet-like MACs
// that differ only in their low byte; raw FNV-1a has poor low-bit avalanche
// on sequential inputs.

#pragma once

#include <cstddef>
#include <cstdint>

#include "CurrentPalette.h"

namespace wisp {

// Two RGBW colors. Indexed [0] / [1]; some callers map [0] → base surface,
// [1] → shade surface. The wisp itself doesn't take an opinion — receivers
// decide which surface gets which color via MSG_OVERRIDE_COLORS surface byte.
struct ColorTuple {
  uint8_t r[2] = {0, 0};
  uint8_t g[2] = {0, 0};
  uint8_t b[2] = {0, 0};
  uint8_t w[2] = {0, 0};
};

// Pure function. Determinism property: same `palette.colors()` snapshot +
// same `mac` + same `shuffleSeed` always returns the same tuple. Edge cases:
//   - Empty palette → {0,0,0,0} for both colors.
//   - Single-color palette (post-dedupe) → that color for both surfaces.
//   - 2+ colors → discrete picks; each surface gets one of the authored
//     stops verbatim (no blending).
// `shuffleSeed` defaults to 0 (legacy behavior unchanged). Bumping it
// re-rolls per-lamp color assignment while keeping intra-seed determinism.
ColorTuple sampleTupleForMac(const CurrentPalette& palette,
                             const uint8_t mac[6],
                             uint32_t shuffleSeed = 0);

}  // namespace wisp
