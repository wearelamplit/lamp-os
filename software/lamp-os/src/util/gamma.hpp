#pragma once

#include <Adafruit_NeoPixel.h>

#include <cstdint>
#include <cstdlib>

namespace lamp {

// The smallest byte whose Adafruit_NeoPixel::gamma8 output is nearest `v`:
// what a colour shown raw must hold to show the same through the gamma the
// LED output applies. Exact for v <= 66, within one above.
inline uint8_t inverseGamma8(uint8_t v) {
  int best = 0;
  int bestErr = 256;
  for (int s = 0; s < 256; ++s) {
    const int err = std::abs(int(Adafruit_NeoPixel::gamma8(uint8_t(s))) - int(v));
    if (err < bestErr) {
      best = s;
      bestErr = err;
    }
  }
  return uint8_t(best);
}

}  // namespace lamp
