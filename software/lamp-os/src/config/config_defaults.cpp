#include "config/config.hpp"

#include "util/color.hpp"

namespace lamp {

static void seedRole(std::vector<SegmentSettings>& segs,
                     const std::vector<Config::SegmentDefault>& seeds, const std::string& hex) {
  if (!seeds.empty()) {
    segs.clear();
    for (const auto& s : seeds) segs.push_back({s.name, s.px, parseColorList(s.colors)});
  } else if (!hex.empty()) {
    segs.front().colors = parseColorList(hex);
  }
}

void applyRoleDefaults(const Config::Defaults& d, config_codec::StoreForm form,
                       BaseSettings& base, ShadeSettings& shade) {
  if (form == config_codec::StoreForm::Empty) {
    seedRole(base.segments, d.baseSegments, d.baseColor);
    seedRole(shade.segments, d.shadeSegments, d.shadeColor);
  }
  base.colorsEditable = d.baseColorsEditable;
  shade.colorsEditable = d.shadeColorsEditable;
  base.rotationSupported = d.rotationSupported;
  shade.rotationSupported = d.rotationSupported;
  base.segments.front().px = resolveConfiguredPx(base.segments.front().px, d.basePx);
  base.knockoutPixels.resize(base.sumPx(), 100);
  shade.segments.front().px = resolveConfiguredPx(shade.segments.front().px, d.shadePx);
}

}  // namespace lamp
