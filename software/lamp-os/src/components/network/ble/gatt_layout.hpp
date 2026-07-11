#pragma once
#include <cstddef>
#include <cstdint>

// Frozen GATT attribute layout — the wire contract between lamp firmware and
// the app. Kept free of Arduino/NimBLE so the native `test_ble_gatt_layout`
// can include it. Three things bind together:
//
//   1. `kGattLayout` below mirrors the createCharacteristic() order in
//      ble_control.cpp. Handles are positional, so this ORDER is the contract.
//   2. The boot-time assert in ble_control.cpp checks the live registration
//      against this table — a mismatch means registration drifted.
//   3. test_ble_gatt_layout pins hash(kGattLayout) to kGattSchemaVersion — you
//      cannot change the layout without bumping the version + re-pinning.
//
// Append-only at the tail; never insert, remove, or reorder a characteristic
// (or add/remove a CCCD via NOTIFY) without bumping kGattSchemaVersion. Payload
// and behavior changes do NOT touch this. See the frozen-layout lock-in in
// CLAUDE.md.

namespace ble_control {

// New in schema v1. The app reads this to detect the lamp's layout version and
// fall back gracefully on legacy lamps that predate the characteristic.
constexpr const char* CHAR_SCHEMA_VERSION = "5f64f4ea-d6d9-4a44-9b3f-3a8d6f7e6b40";

// New in schema v2 (tail). Binary blob of claimed lamp MACs from the wisp.
constexpr const char* CHAR_WISP_CLAIMS    = "5f64f4eb-d6d9-4a44-9b3f-3a8d6f7e6b40";

constexpr uint8_t kGattSchemaVersion = 4;

// Property bits — local mirror of the NimBLE flags so this header stays
// dependency-free. Only the bits that affect the handle layout matter
// (NOTIFY adds a CCCD descriptor = a handle).
enum GattProp : uint8_t {
  GP_READ     = 1 << 0,
  GP_WRITE    = 1 << 1,
  GP_WRITE_NR = 1 << 2,
  GP_NOTIFY   = 1 << 3,
};

struct GattCharDef {
  const char* uuid;
  uint8_t     props;
};

// UUIDs are literals here (not the ble_control.hpp constants, which live behind
// <Arduino.h>); the boot assert guarantees they match the live registration.
constexpr GattCharDef kGattLayout[] = {
    {"5f64f4d1-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // auth
    {"5f64f4d2-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // brightness
    {"5f64f4d3-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // shadeColors
    {"5f64f4d4-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // baseColors
    {"48537d49-11a7-4f54-a69a-9425b9288c50", GP_WRITE | GP_WRITE_NR},   // commit
    {"5f64f4d5-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // baseKnockout
    {"5f64f4e5-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // homeModeFocus
    {"5f64f4e9-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // editSession
    {"5f64f4d6-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // expressionTest
    {"5f64f4d9-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // expressionOp
    {"5f64f4da-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // wifiOp
    {"5f64f4db-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_READ | GP_NOTIFY},      // wifiState
    {"5f64f4dc-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // pageCtrl
    {"5f64f4dd-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_READ},                  // pageData
    {"5f64f4e6-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_READ | GP_WRITE},       // socialDispositions
    {"5f64f4e4-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // remoteOp
    {"5f64f4e1-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // wispOp
    {"5f64f4e2-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_READ | GP_NOTIFY},      // wispStatus
    {"5f64f4d7-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE},                 // settingsBlob
    {"5f64f4e7-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_NOTIFY},     // fwControl
    {"5f64f4e8-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_WRITE | GP_WRITE_NR},   // fwChunk
    {"5f64f4d8-d6d9-4a44-9b3f-3a8d6f7e6b40", GP_NOTIFY},                // stateNotify
    {CHAR_SCHEMA_VERSION,                    GP_READ},                  // schemaVersion
    {CHAR_WISP_CLAIMS,                       GP_READ},                  // wispClaims (v2, tail)
};

constexpr size_t kGattLayoutCount = sizeof(kGattLayout) / sizeof(kGattLayout[0]);

}  // namespace ble_control
