#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "config/config.hpp"

namespace ble_control {

constexpr const char* SERVICE_UUID          = "5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40";

// auth (write-with-response): send lamp.password to unlock the connection
constexpr const char* CHAR_AUTH            = "5f64f4d1-d6d9-4a44-9b3f-3a8d6f7e6b40";
// brightness (write-without-response): single u8 value 0-100. Firmware
// routes to homeMode.brightness when the app has signalled it's on the
// Home Mode page via CHAR_HOME_MODE_FOCUS; otherwise to lamp.brightness.
constexpr const char* CHAR_BRIGHTNESS      = "5f64f4d2-d6d9-4a44-9b3f-3a8d6f7e6b40";
// shade_colors (write-without-response): JSON array of hex color strings
constexpr const char* CHAR_SHADE_COLORS    = "5f64f4d3-d6d9-4a44-9b3f-3a8d6f7e6b40";
// base_colors (write-without-response): JSON array of hex color strings
constexpr const char* CHAR_BASE_COLORS     = "5f64f4d4-d6d9-4a44-9b3f-3a8d6f7e6b40";
// base_knockout (write-without-response): 2 bytes [pixelIndex u8, brightness u8 0-100]
constexpr const char* CHAR_BASE_KNOCKOUT   = "5f64f4d5-d6d9-4a44-9b3f-3a8d6f7e6b40";
// expression_test (write-with-response): UTF-8 expression type name; empty string = complete
constexpr const char* CHAR_EXPRESSION_TEST = "5f64f4d6-d6d9-4a44-9b3f-3a8d6f7e6b40";
// settings_blob (read + write-with-response): full config JSON
constexpr const char* CHAR_SETTINGS_BLOB   = "5f64f4d7-d6d9-4a44-9b3f-3a8d6f7e6b40";
// state_notify (notify): lamp-driven state change notifications
constexpr const char* CHAR_STATE_NOTIFY    = "5f64f4d8-d6d9-4a44-9b3f-3a8d6f7e6b40";
// expression_op (write-with-response): JSON op for runtime expression CRUD
//   {"op":"upsert","entry":{...full expression config...}}
//   {"op":"remove","type":"<type>","target":<1|2|3>}
constexpr const char* CHAR_EXPRESSION_OP   = "5f64f4d9-d6d9-4a44-9b3f-3a8d6f7e6b40";
// wifi_op (write-with-response): JSON op for WiFi management. Only op is
//   {"op":"scan"}: kick off an async scan (results notified via
//                  CHAR_WIFI_STATE).
// Presence-only mode means the lamp never associates, so there's no
// "connect" op; setHomeSsid + forget live in the settings_blob path.
constexpr const char* CHAR_WIFI_OP         = "5f64f4da-d6d9-4a44-9b3f-3a8d6f7e6b40";
// wifi_state (read + notify): JSON snapshot of WiFi state
constexpr const char* CHAR_WIFI_STATE      = "5f64f4db-d6d9-4a44-9b3f-3a8d6f7e6b40";
// Page protocol (auth-gated): paginated lamp→app reads of named sections.
// A section's JSON can exceed the 512 B NimBLE ATT ceiling (a 3-expression
// config serializes to 579 B and is rejected at boot-time setValue), so the
// CTRL+DATA pair streams MTU-sized chunks from a per-connection snapshot.
//
//   CHAR_PAGE_CTRL (write): app writes a UTF-8 section name (e.g. "expr").
//     Lamp snapshots that section's cached JSON into the connection slot
//     and resets a cursor. Subsequent CTRL writes replace the snapshot.
//     Known names: "lamp", "base", "shade", "expr", "home", "nearby".
//   CHAR_PAGE_DATA (read): each read returns the next chunk of bytes
//     starting at the cursor. A read returning fewer bytes than the
//     hardcoded chunk size (244 = ATT_MTU 247 - 3 ATT header) signals
//     "done". App reads until short, then jsonDecodes the concatenated
//     bytes.
//
// Direction-inverse of the OTA CHAR_FW_CONTROL/CHAR_FW_CHUNK pair: OTA
// streams host→lamp (write), pagination streams lamp→host (read).
constexpr const char* CHAR_PAGE_CTRL       = "5f64f4dc-d6d9-4a44-9b3f-3a8d6f7e6b40";
constexpr const char* CHAR_PAGE_DATA       = "5f64f4dd-d6d9-4a44-9b3f-3a8d6f7e6b40";
// remote_op (write-with-response, encrypted): forward a BLE control write
// to a far lamp via ESP-NOW.
constexpr const char* CHAR_REMOTE_OP       = "5f64f4e4-d6d9-4a44-9b3f-3a8d6f7e6b40";
// home_mode_focus (write-without-response): single u8 bool. The app
// writes 1 while the user is on the Home Mode setup page and 0 when
// they leave. While 1, the firmware overrides effectiveHomeMode to
// TRUE (so the user can preview home brightness / behavior) AND
// routes incoming CHAR_BRIGHTNESS writes to config.homeMode.brightness
// instead of config.lamp.brightness. While 0 (and a BT client is
// connected), effectiveHomeMode is forced FALSE: the BT session is
// the "configurator" and shouldn't be in home mode. When no BT client
// is connected, effectiveHomeMode is presence-based: home.enabled +
// home.ssid + wifi::homeSsidVisible(). Cleared automatically on BT
// disconnect.
constexpr const char* CHAR_HOME_MODE_FOCUS = "5f64f4e5-d6d9-4a44-9b3f-3a8d6f7e6b40";
// social_dispositions (read + write, encrypted): JSON map of peer name to
// disposition 1..5 (salty .. neutral .. smitten). Read returns the full
// map; write replaces it. Auth-gated like the other write characteristics.
// Stored separately from the main config blob in NVS namespace "lamp",
// key "dispositions"; survives reboots, doesn't bloat the config blob.
constexpr const char* CHAR_SOCIAL_DISPOSITIONS = "5f64f4e6-d6d9-4a44-9b3f-3a8d6f7e6b40";
// wisp_op (write-with-response, plaintext JSON): forward a wisp-bound op
// from the app over ESP-NOW. The lamp does NOT apply this locally; it
// only broadcasts a MSG_CONTROL_OP carrying the payload, and the wisp(s)
// on the mesh consume it. Open-set by design: the wire format is
// {"char":"wispOp","op":"setZone","zoneId":N,...} and the wisp owns the
// op vocabulary; lamps never interpret. Auth-gated like the rest of the
// write surface.
constexpr const char* CHAR_WISP_OP         = "5f64f4e1-d6d9-4a44-9b3f-3a8d6f7e6b40";
// wisp_status (read + notify): JSON snapshot of the latest wispStatus
// broadcast the lamp has seen, merged with the last MSG_WISP_HELLO data
// on file for the same wisp. Auth-gated. Notifies whenever the cache
// updates (drain of pendingWispStatus).
constexpr const char* CHAR_WISP_STATUS     = "5f64f4e2-d6d9-4a44-9b3f-3a8d6f7e6b40";

// fw_control (write + notify): app pushes MSG_FW_OFFER and MSG_FW_DONE
// frames (lamp_protocol wire format, no envelope) and receives
// MSG_FW_ACCEPT, MSG_FW_REQ, and MSG_FW_RESULT back as notifications.
// Auth-gated. Single in-flight OTA per lamp (mutex enforced in
// FirmwareReceiver); a write while another source is mid-flow yields a
// DeclineBusy notification.
constexpr const char* CHAR_FW_CONTROL      = "5f64f4e7-d6d9-4a44-9b3f-3a8d6f7e6b40";
// fw_chunk (write-without-response): app streams MSG_FW_CHUNK payloads.
// Auth-gated. Frequency is high (~one chunk per BLE PDU); the write
// callback parses the frame on the BLE host task and calls
// FirmwareReceiver::handleChunkOnRecvTask directly, same fast path as
// the ESP-NOW chunk handler.
constexpr const char* CHAR_FW_CHUNK        = "5f64f4e8-d6d9-4a44-9b3f-3a8d6f7e6b40";

// CHAR_EDIT_SESSION (auth-gated, WRITE_NR): app signals "operator has
// the colour-picker / brightness-slider for surface X open" so the
// lamp can drop wisp-sourced overrides for that surface until the
// picker closes. Payload is 2 bytes [surface, state] with:
//   surface: 0x01 = Base, 0x02 = Shade, 0x04 = Brightness
//   state:   0x00 = closed (clear flag), 0x01 = open (set flag)
// Multiple-surface picker sessions are handled by sending one frame
// per surface. Cleared on BLE disconnect.
constexpr const char* CHAR_EDIT_SESSION    = "5f64f4e9-d6d9-4a44-9b3f-3a8d6f7e6b40";
// CHAR_COMMIT (write-with-response, auth-gated): parameterless commit
// signal. Payload bytes are semantically ignored: the arrival of the
// write IS the signal. Firmware persists the current in-memory config
// to NVS, debounced 1500ms, OTA-gated, FNV-1a hash-deduped.
inline constexpr const char* CHAR_COMMIT_UUID = "48537d49-11a7-4f54-a69a-9425b9288c50";

/**
 * @brief Start the BLE GATT control service.
 */
void start(lamp::Config* config);

void stop();
bool isRunning();

/**
 * @brief Per-loop housekeeping on Core 1. Rebuilds any dirty section JSON
 *        cache so a CHAR_PAGE_CTRL page read on Core 0 finds it populated
 *        and never serializes JSON inside the NimBLE callback. Cheap when
 *        nothing is dirty. Call once per main-loop iteration.
 */
void tick();

/**
 * @brief Send a state-change notification to all subscribed clients.
 *        Payload is `{"previewActive":<bool>}`, used by the app's Test
 *        button to debounce its label without an app-side timer.
 */
void notifyStateChange();

/**
 * @brief Send a WiFi-state-change notification on CHAR_WIFI_STATE.
 */
void notifyWifiState();

/**
 * @brief Push a CHAR_WISP_STATUS notification. Called from the loop
 *        drain on Core 1 after pendingWispStatus updates the cache via
 *        NearbyLamps::cacheWispStatus. The notify payload is the same
 *        merged JSON the on-read callback serves.
 */
void notifyWispStatus();

/**
 * @brief True while a BT client is currently connected to this lamp.
 *        Used by effective-home-mode logic and by wifi::tick() to skip
 *        background scans during BT sessions.
 */
bool isClientConnected();

/**
 * @brief True while the app has signalled it's on the Home Mode setup
 *        page (via CHAR_HOME_MODE_FOCUS = 1). Cleared on BT disconnect.
 */
bool isHomeModePageActive();

/**
 * @brief True while the central scan is paused because a GATT client is
 *        connected. bluetooth.cpp's onScanEnd queries this to decide
 *        whether to auto-restart the scan. Flipped on GATT connect /
 *        disconnect inside ble_control.cpp.
 */
bool isScanPaused();

/**
 * @brief Stop BLE advertising + scan to free the radio for ESP-NOW
 *        during OTA. ESP32-WROOM shares one 2.4 GHz radio between BLE and
 *        WiFi; the IDF coex arbiter gates WiFi RX during BLE adv/scan
 *        windows. For OTA's narrow OFFER↔ACCEPT handshake (and chunk
 *        stream), the coex pressure makes broadcasts drop on the floor.
 *        Pausing BLE for the OTA window (1-3 sec on sender for handshake,
 *        ~minutes on receiver for chunk receive) cuts that loss entirely.
 *        Cost: phone app briefly loses connection. Acceptable for OTA.
 *        Idempotent.
 */
void pauseRadioForOta();

/**
 * @brief Restart BLE advertising + scan after pauseRadioForOta(). Called
 *        on OTA success or any failure path. Idempotent.
 */
void resumeRadioAfterOta();

/**
 * @brief Force-disconnect any active GATT client. Called from
 *        ota_quiet_mode::enterQuiet when tearDownRadio=true (mesh OTA
 *        path). Keeps the GATT server up; only kicks the connected
 *        peer so it can't send writes that compete for radio time with
 *        the chunk stream. Idempotent.
 */
void disconnectGattClientsForOta();

// Posts a commit signal from the BLE callback (Core 0) to the loop task
// (Core 1). Loop drain debounces and calls config.persistConfig. Signature
// matches WriteRouter::PostFn; the data/len bytes are semantically ignored
// (the arrival of the write IS the signal).
void postPendingCommit(const char* data, size_t len);

}  // namespace ble_control

namespace lamp { class FirmwareReceiver; }

namespace ble_control {

/**
 * @brief Wire the FirmwareReceiver instance into the BLE OTA dispatch.
 *        Called from lamp.cpp after firmwareReceiver.begin().
 *        Once registered, CHAR_FW_CONTROL writes (OFFER/DONE) and
 *        CHAR_FW_CHUNK writes route into the receiver, and the receiver's
 *        BLE transport notifies ACCEPT/REQ/RESULT back on CHAR_FW_CONTROL.
 */
void setFirmwareReceiver(lamp::FirmwareReceiver* receiver);

}  // namespace ble_control
