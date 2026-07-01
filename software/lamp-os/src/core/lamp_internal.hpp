// software/lamp-os/src/core/lamp_internal.hpp
//
// Private header — internal to core/lamp.cpp + core/lamp_drains.cpp. Do not include from any other TU.
//
// Exposes the file-scope statics and helpers that the drain method bodies
// (split out into lamp_drains.cpp for readability) need to reach back into.
// The DEFINITIONS still live in lamp.cpp — this header only carries
// `extern` declarations and a couple of forward declarations.
//
// Storage ownership: lamp.cpp. Readers: lamp.cpp + lamp_drains.cpp only.

#pragma once

#include <Arduino.h>

#include <cstdint>

#include "behaviors/configurator.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/network/bluetooth.hpp"
#include "components/network/mesh_link.hpp"
#include "config/config.hpp"
#include "config/nvs_config_store.hpp"
#include "expressions/expression_manager.hpp"

// ── File-scope statics owned by lamp.cpp ─────────────────────────────────

// Per-pixel knockout staging slot — drain reads + clears, BLE callback
// fills under portMUX. Struct definition is small so it lives here in
// full rather than as an opaque type.
struct PendingKnockoutUpdate {
  bool valid = false;
  uint8_t pixel = 0;
  uint8_t brightness = 100;
};

extern PendingKnockoutUpdate pendingKnockout;

// Single-byte staging slot for CHAR_BRIGHTNESS writes. -1 = empty.
extern volatile int8_t pendingBrightness;

// Set on Core 0 (BLE onDisconnect) to force a synchronous disposition
// NVS commit on Core 1. Drain tick consumes.
extern volatile bool pendingFlushDispositionsRequested;

// HwConfig-sourced brightness cap. Seeded in Lamp::setup() from
// hw_.maxBrightness so file-scope helpers can use the right ceiling
// without importing a variant header.
extern uint8_t s_hwMaxBrightness;

// Cross-core mux shared by every pending slot post / drain pair.
extern portMUX_TYPE pendingMux;

// Singletons owned by lamp.cpp.
extern lamp::NvsConfigStore configStore;
extern lamp::BluetoothComponent bt;
extern lamp::FirmwareReceiver firmwareReceiver;
extern lamp::MeshLink meshLink;
extern lamp::ExpressionManager expressionManager;
extern lamp::ConfiguratorBehavior shadeConfiguratorBehavior;
extern lamp::ConfiguratorBehavior baseConfiguratorBehavior;

// ── File-scope helpers owned by lamp.cpp ─────────────────────────────────

// Selects the transport-flavour branch inside applyRemoteOpRouted. The
// BLE drain passes BLE so the router unwraps the targetMac envelope;
// the ESP-NOW drain passes EspNow so the router dispatches directly
// (the WiFi task already did the for-us / rebroadcast decision).
enum class RemoteOpTransport { BLE, EspNow };

// Unified cascade-receive router — see lamp.cpp definition for the
// full BLE-vs-EspNow asymmetry note. Used by drainInboundOp / drainRemoteOp.
void applyRemoteOpRouted(const char* payloadJson, size_t len,
                         const uint8_t srcMac[6],
                         RemoteOpTransport origin);

// Test-panel action dispatcher — drainTestAction calls this with a parsed
// JsonDocument. Defined in lamp.cpp.
void dispatchLampAction(JsonDocument& doc, unsigned long updateTimeMs);
