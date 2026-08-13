#pragma once

// lamp_protocol umbrella for the lamp firmware. Pulls the shared mesh wire
// core (header + presence + control_op + wisp + override + dedup_ring +
// MAX_PACKET_SIZE) from the lampos-protocol library, then layers the
// lamp-only families (OTA/FS, directed command/color, event) on top. The
// wisp compiles only the shared core.
//
// Shared core lives in software/shared/protocol/; the wisp includes the same
// <lampos/protocol/lamp_protocol.hpp>. Lamp-only families stay here beside
// this shim.

#include <lampos/protocol/lamp_protocol.hpp>

#include "command_auth.hpp"
#include "command.hpp"
#include "color_info.hpp"
#include "event.hpp"
#include "fw_ota.hpp"
