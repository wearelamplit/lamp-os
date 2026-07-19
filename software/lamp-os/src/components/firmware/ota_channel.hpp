#pragma once

#include <cstdint>

// Returns true when a lamp at (ourChannel, ourVersion) should accept firmware
// offered at (offerChannel, offerVersion).
//
// Intra-channel: same channel, offerVersion strictly greater than ourVersion.
// Promotion: ourChannel ends in "-beta", offerChannel ends in "-stable",
//   the {type} prefix matches, and offerVersion >= ourVersion.
// All other combinations (cross-variant, stable<-beta, downgrade) return false.
bool otaAcceptable(const char* ourChannel,
                   uint32_t    ourVersion,
                   const char* offerChannel,
                   uint32_t    offerVersion);
