// software/lamp-os/src/core/pending_slot_aggregate.hpp
//
// Aggregates the PendingJsonSlot / PendingTypedSlot instances + the two
// commit-flag volatiles that used to live at file scope in
// lamp.cpp. Lifted into the framework so the Lamp base class
// owns the Core-0 → Core-1 hand-off pattern, not each lamp subclass.
//
// The slots themselves are templated in pending_json_slot.hpp /
// pending_typed_slot.hpp — those headers stay where they are. This
// aggregate just bundles the production instances.

#pragma once

#include "core/pending_json_slot.hpp"
#include "core/pending_typed_slot.hpp"
#include "components/network/mesh/pending_slots.hpp"
#include "components/firmware/firmware_receiver.hpp"

namespace lamp {

// Size constants — match the originals in lamp.cpp.
inline constexpr size_t kPendingJsonBase = 256;
inline constexpr size_t kPendingJsonOp   = 512;  // expression op payloads are larger

struct PendingSlotAggregate {
  // Commit flags — set by ble_control.cpp's CHAR_COMMIT path, drained by
  // the loop in lamp.cpp's commit-tick logic.
  volatile bool pendingCommit = false;
  volatile bool forceCommitFlush = false;

  // Section colour writes (CHAR_BASE_SECTION / CHAR_SHADE_SECTION).
  PendingJsonSlot<kPendingJsonBase> baseColors;
  PendingJsonSlot<kPendingJsonBase> shadeColors;

  // Op writes (CHAR_OP / mesh-relayed CONTROL_OP).
  PendingJsonSlot<kPendingJsonOp> expressionOp;
  PendingJsonSlot<kPendingJsonOp> wifiOp;
  PendingJsonSlot<kPendingJsonOp> testAction;
  PendingJsonSlot<kPendingJsonOp> remoteOp;
  PendingJsonSlot<kPendingJsonOp> wispOp;
  PendingJsonSlotWithMac<kPendingJsonOp> wispStatus;
  PendingJsonSlot<kPendingJsonOp> socialDispositions;
  PendingJsonSlot<kPendingJsonOp> settingsBlob;
  PendingJsonSlotWithMac<kPendingJsonOp> inboundOp;

  // Typed slots — transient color/brightness overrides + wisp events +
  // firmware control flow.
  PendingTypedSlot<PendingOverrideColors>     overrideColors;
  PendingTypedSlot<PendingRestoreColors>      restoreColors;
  PendingTypedSlot<PendingOverrideBrightness> overrideBrightness;
  PendingTypedSlot<PendingRestoreBrightness>  restoreBrightness;
  PendingTypedSlot<PendingWispHello>          wispHello;
  PendingTypedSlot<PendingWispPalette>        wispPalette;
  PendingTypedSlot<PendingWispClaim>          wispClaim;
  PendingTypedSlot<PendingWispPaint>          wispPaint;
  PendingTypedSlot<PendingCommand>            command;
  PendingTypedSlot<PendingEvent>              event;
  PendingTypedSlot<PendingFirmwareControl>    firmwareControl;
};

// Single production instance, defined in pending_slot_aggregate.cpp.
// File-scope per the single-instance-per-binary design (see lamp.cpp
// header comment) — ble_control.cpp + lamp.cpp reach it via extern.
extern PendingSlotAggregate pendingSlots;

}  // namespace lamp
