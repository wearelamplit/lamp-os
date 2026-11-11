#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"
#include "expressions/yawn/yawn_math.hpp"

namespace lamp {

inline constexpr const char* kYawnDurationKey = "yawnDuration";
inline constexpr const char* kYawnDepthKey = "yawnDepth";
inline constexpr const char* kYawnLiftKey = "yawnLift";
inline constexpr const char* kYawnHoldKey = "yawnHold";
inline constexpr const char* kYawnRecoverKey = "yawnRecover";
inline constexpr const char* kYawnWarmthKey = "yawnWarmth";
inline constexpr const char* kYawnLagKey = "yawnLag";

// The inhale's length. Its height is the yawnLift param.
inline constexpr uint32_t kYawnInhaleMs = 600;

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr ParamSpec kYawnParams[] = {
  { .key = kYawnDurationKey, .kind = ParamKind::Int, .label = "Length",
    .min = 2000, .max = 12000, .step = 100, .def = 4500, .unit = "ms",
    .help = "The whole yawn, from the inhale to back at the scene." },
  { .key = kYawnDepthKey, .kind = ParamKind::Int, .label = "Dims to",
    .min = 5, .max = 95, .step = 5, .def = 70, .unit = "%",
    .help = "Brightness at the bottom of the yawn, as a share of the scene." },
  { .key = kYawnLiftKey, .kind = ParamKind::Int, .label = "Inhale lift",
    .min = 0, .max = 30, .step = 1, .def = 8, .unit = "%",
    .help = "How far the scene brightens on the inhale before it dims." },
  { .key = kYawnHoldKey, .kind = ParamKind::Int, .label = "Hold",
    .min = 0, .max = 3000, .step = 100, .def = 500, .unit = "ms" },
  { .key = kYawnRecoverKey, .kind = ParamKind::Int, .label = "Recover",
    .min = 300, .max = 6000, .step = 100, .def = 1800, .unit = "ms" },
  { .key = kYawnWarmthKey, .kind = ParamKind::Int, .label = "Warmth",
    .min = 0, .max = 100, .step = 5, .def = 30, .unit = "%",
    .help = "How much warmer the scene turns at the bottom." },
  { .key = kYawnLagKey, .kind = ParamKind::Int, .label = "Base lag",
    .min = 0, .max = 1000, .step = 50, .def = 0, .unit = "ms",
    .help = "How far the base trails the shade when the yawn runs on both." },
  kOpacityParam,
};
inline constexpr ExpressionDescriptor kYawnDescriptorData{
  .id       = "yawn",
  .name     = "Yawn",
  .colors   = { .max = 0 },
  .interval = RangeSpec{
    .min    = 300,
    .max    = 18000,
    .step   = 30,
    .unit   = "s",
    .defLo  = 1800,
    .defHi  = 7200,
    .help   = kIntervalHelp,
  },
  .params        = kYawnParams,
  .spansSurfaces = true,
};

// A slow dim-and-recover of the scene below it: a short inhale lift, a long
// dim, a hold at the bottom, then a recovery that lands exactly on the scene.
// On both surfaces the base trails the shade by the yawnLag param.
class YawnExpression : public Expression {
 public:
  explicit YawnExpression(FrameBuffer* inBuffer, uint32_t inFrames = 1);

  static const ExpressionDescriptor& classDescriptor();
  const ExpressionDescriptor& descriptor() const override;

  void configureFromParameters(const ExpressionParams& parameters) override;
  void joinSurfaceAbove(FrameBuffer* above) override { above_ = above; }
  void draw() override;

 protected:
  // Live while either joined surface is.
  using Expression::shouldAffectBuffer;
  bool shouldAffectBuffer() override;

  void onTrigger() override;

 private:
  void yawnSurface(FrameBuffer* surface, uint32_t elapsedMs);

  YawnShape shape_;
  float warmth_ = 0.0f;
  uint32_t lagMs_ = 0;
  FrameBuffer* above_ = nullptr;  // shade, leading the base fb by lagMs_
  uint64_t startMs_ = 0;
};

}  // namespace lamp
