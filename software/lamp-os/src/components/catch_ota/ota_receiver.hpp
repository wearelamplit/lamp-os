#pragma once

// Single-threaded mesh-OTA receive state machine for the OLD main firmware.
//
// Everything here runs on the loop task: the ESP-NOW recv trampoline only
// enqueues frames (espnow_link's SPSC ring), and the orchestrator drains the
// ring on the loop task and dispatches OFFER/CHUNK/DONE here. There is NO
// cross-core chunk write path, so a plain `state_ == Streaming` check guards
// the chunk path.
//
// The lamp stays web-controllable (softAP up) during discovery; onOffer()
// quiesces the radio to STA-only (radioEnterOtaMode) at the committed OFFER, and
// re-registers the sender peer afterward since that mode switch clears the
// ESP-NOW peer table.

#include <cstddef>
#include <cstdint>

#include "ota_protocol.hpp"

namespace catch_ota {
namespace ota_receiver {

// OFFER intake: pick the inactive OTA partition, erase the entire image region
// up front (before ACCEPT, while nothing is in flight), then ACCEPT. devMac is
// the OFFER sender — the target of every ACCEPT/REQ/RESULT this session.
//
// At the new-flow commit point (before the erase) this records one
// rollback-breaker attempt — exactly once per genuine transfer, since re-OFFERs
// of an in-progress session take the busy/re-ACK path and never commit. Any
// post-commit failure reboots to restore main's radio, so the counted attempt
// is what bounds a deterministic-failure reboot loop.
void onOffer(const ParsedFwOffer& offer, const uint8_t devMac[6]);

// CHUNK intake on the loop task. Writes p.bytes to the inactive partition at
// p.offset and marks the chunk bit. Dropped silently unless streaming.
void onChunkOnLoop(const ParsedFwChunk& chunk);

// DONE intake: when the bitmap is full, verify the signed image and (only on a
// full pass) flip the boot partition + reboot; otherwise REQ the missing run.
void onDone(const ParsedFwDone& done);

// Loop-task pump: drains the ACCEPT burst, runs the stall / no-progress
// watchdogs, and emits REQ recovery. Cheap when idle.
void tick(uint32_t nowMs);

// True between accepting an OFFER and verify/apply. Drives HELLO suppression.
bool isInProgress();

}  // namespace ota_receiver
}  // namespace catch_ota
