// software/lamp-os/src/core/pending_slot_aggregate.cpp
#include <Arduino.h>  // portMUX_TYPE, portENTER_CRITICAL (needed by pending_json_slot.hpp templates)
#include "core/pending_slot_aggregate.hpp"

namespace lamp {
PendingSlotAggregate pendingSlots;
}  // namespace lamp
