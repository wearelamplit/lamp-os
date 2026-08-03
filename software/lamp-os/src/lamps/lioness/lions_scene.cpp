#include "lamps/lioness/lions_scene.hpp"

#include "expressions/primitives.hpp"
#include "util/gradient.hpp"

namespace lamp { namespace lioness {

void renderZone(std::vector<Color>& out, const Zone& zn,
                const std::vector<Color>& stops, Color mainColor) {
  const uint16_t sz = zn.size();
  if (sz == 0) return;
  if (stops.empty()) {
    for (uint16_t i = 0; i < sz; ++i) out[zn.posMin + i] = mainColor;
    return;
  }
  std::vector<Color> grad = buildGradientWithStops(static_cast<uint8_t>(sz), stops);
  for (uint16_t i = 0; i < sz; ++i)
    out[zn.posMin + i] = (i < grad.size()) ? grad[i] : mainColor;
}

void renderLions(std::vector<Color>& out, uint16_t windowSize,
                 const std::array<std::vector<Color>, 3>& lionStops,
                 Color mainColor) {
  out.assign(windowSize, mainColor);
  const std::vector<Zone> zones = evenZones(3, windowSize);
  for (size_t z = 0; z < zones.size() && z < 3; ++z)
    renderZone(out, zones[z], lionStops[z], mainColor);
}

}}  // namespace lioness, namespace lamp
