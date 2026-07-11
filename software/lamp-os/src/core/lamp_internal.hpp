// software/lamp-os/src/core/lamp_internal.hpp
//
// Private header. Internal to the lamp.cpp TU family:
//   core/lamp.cpp, core/lamp_drains.cpp, core/lamp_remote_op.cpp,
//   core/lamp_brightness.cpp, core/lamp_behaviors.cpp, core/lamp_test_action.cpp
//
// Exposes the file-scope statics and helpers that the split method bodies
// need to reach back into lamp.cpp state. The DEFINITIONS still live in
// lamp.cpp. This header only carries `extern` declarations and
// cross-TU function declarations.
//
// Storage ownership: lamp.cpp. Do not include from any other TU.

#pragma once

#include <Arduino.h>

#include <cstdint>

#include "behaviors/configurator.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/network/ble/bluetooth.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "config/nvs_config_store.hpp"
#include "core/lamp.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/expression_observer.hpp"

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
extern lamp::ExpressionObserverRegistry expressionObserverRegistry;
extern lamp::ConfiguratorBehavior shadeConfiguratorBehavior;
extern lamp::ConfiguratorBehavior baseConfiguratorBehavior;
extern lamp::Config config;

// Forward declarations for the types used in the externs below.
namespace lamp {
class FrameBuffer;
class Compositor;
class SocialBehavior;
class FadeOutBehavior;
class KnockoutBehavior;
}  // namespace lamp

extern lamp::FrameBuffer shade;
extern lamp::FrameBuffer base;
extern lamp::Compositor compositor;
extern lamp::SocialBehavior shadeSocialBehavior;
extern lamp::FadeOutBehavior shadeFadeOutBehavior;
extern lamp::FadeOutBehavior baseFadeOutBehavior;
extern lamp::KnockoutBehavior baseKnockoutBehavior;

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
// JsonDocument. Defined in lamp_test_action.cpp.
void dispatchLampAction(JsonDocument& doc, unsigned long updateTimeMs);

// ── Cross-TU function declarations ───────────────────────────────────────
// These functions lost their `static` qualifier when extracted from lamp.cpp
// into sibling TUs; they remain private by living only in this header.

// lamp_brightness.cpp
bool calculateEffectiveHomeMode();
uint8_t effectiveBrightness();
void reapplyHomeModeState();

// lamp_behaviors.cpp
void initBehaviors(lamp::Features features, lamp::Lamp& self);
