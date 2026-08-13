#pragma once
// Single source for each variant's editable-expression NAME list, consumed by
// BOTH the runtime registration (Lamp::registerExpressions + variant overrides)
// and the host catalog generator. Regular naming lets one X-macro drive both:
// the runtime expands each stem to <Name>Expression::classDescriptor(), the
// generator to k<Name>DescriptorData. Names only, no Arduino/hardware deps, so
// the host tool and the framework can both include it.
//
// Bloom is internal (serializeCatalog skips it); it rides staff's list so the
// runtime set and the generated catalog stay symmetric on both sides.

#define LAMPOS_BASE_EXPRESSIONS(X) \
  X(Glitchy) X(Pulse) X(Breathing) X(Shifty) X(Spotty) X(Shimmer)

#define LAMPOS_SNAFU_EXPRESSIONS(X) \
  X(Glitchy) X(Pulse) X(Spotty) X(Shimmer)

#define LAMPOS_STAFF_EXPRESSIONS(X) LAMPOS_BASE_EXPRESSIONS(X) X(Bloom)
