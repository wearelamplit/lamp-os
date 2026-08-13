#pragma once
#include <array>
#include <vector>

#include "expressions/primitives.hpp"
#include "util/color.hpp"

namespace lamp { namespace lioness {

// Paint one lion zone into `out` (which must already span the zone). Empty
// stops render solid `mainColor` (idle: mirror Main); otherwise
// buildGradientWithStops(zoneSize, stops), with any gradient tail past its
// length falling back to `mainColor`. Pure: no crossfade, no time.
void renderZone(std::vector<Color>& out, const Zone& zn,
                const std::vector<Color>& stops, Color mainColor);

// Fill `out` (resized to windowSize) with three evenly-tiled lion zones via
// renderZone. The caller blends successive scenes with util/fade.
void renderLions(std::vector<Color>& out, uint16_t windowSize,
                 const std::array<std::vector<Color>, 3>& lionStops,
                 Color mainColor);

}}  // namespace lioness, namespace lamp
