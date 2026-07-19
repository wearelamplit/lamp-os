#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lampos {
namespace led {

// Fraction of the inputs' mean chroma the blend must retain before the guard
// pulls it back along a hue. Bench knob: lower lets blends grey out more.
constexpr float kIdentityChromaGuard = 0.5f;

// Single representative RGBW for a gradient's stops, blended in linear light
// (de-gamma square, weighted mean, re-gamma sqrt) so a two-stop palette leans
// toward its dominant color instead of averaging muddy in gamma space. A
// chroma guard nudges a near-grey blend of complementary stops back along the
// result's hue (or the first stop's, when the result is fully neutral).
// C is any RGBW POD with uint8_t r, g, b, w.
template <typename C>
C blendedIdentity(const C* stops, size_t n) {
  if (n == 0) return C{};
  if (n == 1) return stops[0];

  float rl = 0, gl = 0, bl = 0, wl = 0, inChroma = 0;
  for (size_t i = 0; i < n; ++i) {
    const float r = stops[i].r / 255.0f, g = stops[i].g / 255.0f,
                b = stops[i].b / 255.0f, w = stops[i].w / 255.0f;
    rl += r * r;
    gl += g * g;
    bl += b * b;
    wl += w * w;
    inChroma += std::max({r, g, b}) - std::min({r, g, b});
  }
  const float inv = 1.0f / n;
  float r = std::sqrt(rl * inv), g = std::sqrt(gl * inv), b = std::sqrt(bl * inv);
  const float w = std::sqrt(wl * inv);
  inChroma *= inv;

  const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
  const float chroma = mx - mn, target = kIdentityChromaGuard * inChroma;
  if (inChroma > 0 && chroma < target) {
    const float mid = (mx + mn) * 0.5f;
    float hr = r, hg = g, hb = b;
    if (chroma <= 1e-4f) {
      hr = stops[0].r / 255.0f;
      hg = stops[0].g / 255.0f;
      hb = stops[0].b / 255.0f;
    }
    const float hmx = std::max({hr, hg, hb}), hmn = std::min({hr, hg, hb});
    const float hc = hmx - hmn, hmid = (hmx + hmn) * 0.5f;
    if (hc > 1e-4f) {
      const float k = target / hc;
      r = mid + (hr - hmid) * k;
      g = mid + (hg - hmid) * k;
      b = mid + (hb - hmid) * k;
    }
  }

  auto q = [](float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return C{q(r), q(g), q(b), q(w)};
}

}  // namespace led
}  // namespace lampos
