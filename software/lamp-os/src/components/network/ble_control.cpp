#include "ble_control.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>

#include "config/config.hpp"
#include "behaviors/fade_out.hpp"  // fadeOutRebootRequested flag
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "components/webapp/webapp.hpp"  // shutdownForOta during OTA
#include "util/color.hpp"
#include "util/proximity.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/override_aggregate.hpp"
#include "core/ota_quiet_mode.hpp"  // isQuiet() gate on live-control writes
#include "bluetooth.hpp"  // for BLE_GAP_SCAN_TIME_MS
#include "crypto.hpp"
#include "components/network/gatt_layout.hpp"  // kGattLayout, kGattSchemaVersion
#include "expressions/expression_manager.hpp"
#include "nearby_lamps.hpp"
#include "show_receiver.hpp"
#include "wifi.hpp"
#include "write_router.hpp"

// Defined in lamp.cpp. Each BLE callback does ONLY a fixed-size byte copy
// into a pending slot: zero heap allocation on Core 0. The loop task on
// Core 1 drains the slots and does all heap work (JSON parse, vector build,
// gradient construction, timestamp updates).
void postPendingBrightness(int8_t level);
void postPendingShadeColorsJson(const char* data, size_t len);
void postPendingBaseColorsJson(const char* data, size_t len);
void postPendingKnockout(uint8_t pixel, uint8_t brightness);
void postPendingExpressionOpJson(const char* data, size_t len);
void postPendingWifiOpJson(const char* data, size_t len);
void postPendingTestActionJson(const char* data, size_t len);
void postPendingRemoteOpJson(const char* data, size_t len);
void postPendingSettingsBlobJson(const char* data, size_t len);
void postPendingSocialDispositionsJson(const char* data, size_t len);
// CHAR_WISP_OP: plaintext JSON, app-originated, dedicated slot whose drain
// broadcasts as MSG_CONTROL_OP. Bypasses applyRemoteOpLocal so a
// gossip-relayed wispOp does NOT get re-applied on every lamp; only the
// wisp(s) on the mesh consume it.
void postPendingWispOpJson(const char* data, size_t len);
void postPendingApplyEffectiveBrightness();
// NVS commits cannot run on Core 0 (NimBLE host task). onDisconnect
// posts this flag; the loop drain on Core 1 force-flushes dispositions
// so a phone walk-off still persists the user's slider value.
void postPendingFlushDispositions();
// CHAR_COMMIT volatile state lives in lamp::pendingSlots (pending_slot_aggregate.hpp).
// Written from Core 0 BLE callbacks here; drained on Core 1 in lamp.cpp.

// Owned by lamp.cpp. Read by notifyStateChange() to populate the
// previewActive bit so the app can disable / morph its Test button
// without an app-side timer.
extern lamp::ExpressionManager expressionManager;

namespace ble_control {

static NimBLEServer*         s_server          = nullptr;
static NimBLEService*        s_service         = nullptr;
static NimBLECharacteristic* s_stateNotify     = nullptr;
static NimBLECharacteristic* s_wifiStateChar   = nullptr;
static NimBLECharacteristic* s_wispStatusChar  = nullptr;
static lamp::Config*         s_config      = nullptr;
static bool                  s_running     = false;

// Cross-core flags driven from BLE callbacks (Core 0) and read from the
// loop task (Core 1) via the public accessors below. volatile because
// the compiler can otherwise cache the read in a register on Core 1 and
// miss the flip.
//   s_clientConnected: a BT client (the app) is currently connected.
//     Used by wifi::tick to skip background scans during BT sessions, and
//     by the effective-home-mode gate to swap to "configurator" semantics.
//   s_homeModePageActive: the app has signalled (via CHAR_HOME_MODE_FOCUS)
//     that the user is on the Home Mode setup page. Forces effectiveHomeMode
//     TRUE while BT is up, and routes incoming CHAR_BRIGHTNESS writes to
//     home.brightness instead of lamp.brightness.
static volatile bool         s_clientConnected   = false;
static volatile bool         s_homeModePageActive = false;
// Set true on GATT connect, cleared on GATT disconnect. Queried by
// bluetooth.cpp's onScanEnd via isScanPaused() so the central scan
// doesn't auto-restart while a phone is using the GATT control service.
// volatile because it's written from the BLE host task (Core 0) and read
// from the scan callback (also Core 0 today, but treat as cross-task).
static volatile bool         s_scanPausedForGattClient = false;

// BLE connection interval: request 15-30 ms (TIGHT) on connect and hold it
// for the BT session. Live-preview throughput needs the tight interval; the
// mesh relies on ESP-NOW HELLO / MSG_EVENT being resilient to coex drops
// rather than widening this to free airtime.
static          uint16_t     s_currentConnHandle = 0xFFFF;

static constexpr uint16_t kTightMinUnits = 12;   //  15.0 ms (units of 1.25 ms)
static constexpr uint16_t kTightMaxUnits = 24;   //  30.0 ms
static constexpr uint16_t kSupervisionTimeoutUnits = 400;  // 4.0 s (units of 10 ms)

bool isClientConnected()   { return s_clientConnected;   }
bool isHomeModePageActive() { return s_homeModePageActive; }
bool isScanPaused()        { return s_scanPausedForGattClient; }

// Per-connection state. NimBLE caps simultaneous connections at
// CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3, so a fixed-size array (linear scan
// over 3 entries, touched on every encrypted BLE write) avoids heap
// fragmentation on the BLE hot path.
//
//   handle == kUnusedHandle: slot is free
//   authed:    set true on plaintext auth success or after a successful
//              GCM decrypt (GCM tag IS auth)
//   crypto:    per-connection nonce replay window, lazy-init on first
//              ciphertext write
//   pageSnapshot/Cursor/Mtu: per-conn paginated read state. snapshot is
//              populated on CHAR_PAGE_CTRL onWrite; cursor advances on
//              CHAR_PAGE_DATA onRead. assign() keeps the std::string's
//              existing capacity across sections so the heap sees
//              growth-only, not churn.
struct ConnSlot {
  uint16_t                    handle;
  bool                        authed;
  lamp::crypto::PerConnState  crypto;
  std::string                 pageSnapshot;
  uint16_t                    pageCursor;
  uint16_t                    pageMtu;
};
static constexpr uint16_t kUnusedHandle = 0xFFFF;
static constexpr size_t   kMaxConns     = 3;
static std::array<ConnSlot, kMaxConns> s_conn{{
  {kUnusedHandle, false, {}, {}, 0, 0},
  {kUnusedHandle, false, {}, {}, 0, 0},
  {kUnusedHandle, false, {}, {}, 0, 0},
}};

// Hard ceiling on the chunk size returned by CHAR_PAGE_DATA. Pinned to the
// app's requested ATT_MTU 247 minus the 3-byte ATT header rather than the
// per-conn negotiated MTU: flutter_blue_plus 2.x doesn't reliably surface
// the negotiated value, and a fixed wire constant lets the helper hardcode
// "short = done" without threading MTU state. A smaller negotiated MTU for
// a given conn is used instead (see PageDataCallback::onRead below).
static constexpr uint16_t kPageMaxChunkSize = 244;

// Find the slot owned by [handle], or nullptr if none. Linear scan over
// kMaxConns (=3) entries. static so the compiler can inline it into every
// BLE callback below (per-write hot path).
static ConnSlot* findSlot(uint16_t handle) {
  for (auto& s : s_conn) {
    if (s.handle == handle) return &s;
  }
  return nullptr;
}

// Find the slot for [handle], allocating the first unused slot if there's
// no match. Returns nullptr only if all 3 slots are taken by other handles
// (NimBLE's connection cap makes that unreachable). Newly allocated slots
// have authed=false and an empty crypto state.
static ConnSlot* findOrAllocSlot(uint16_t handle) {
  ConnSlot* freeSlot = nullptr;
  for (auto& s : s_conn) {
    if (s.handle == handle) return &s;
    if (!freeSlot && s.handle == kUnusedHandle) freeSlot = &s;
  }
  if (freeSlot) {
    freeSlot->handle = handle;
    freeSlot->authed = false;
    freeSlot->crypto = lamp::crypto::PerConnState{};
    freeSlot->pageSnapshot.clear();  // keeps capacity
    freeSlot->pageCursor = 0;
    freeSlot->pageMtu    = 0;
  }
  return freeSlot;
}

// Release the slot owned by [handle] back to the pool. No-op if [handle]
// isn't tracked.
static void freeSlot(uint16_t handle) {
  if (auto* s = findSlot(handle)) {
    s->handle = kUnusedHandle;
    s->authed = false;
    s->crypto = lamp::crypto::PerConnState{};
    s->pageSnapshot.clear();  // keeps capacity
    s->pageCursor = 0;
    s->pageMtu    = 0;
  }
}

// Reset every slot. Used on stop().
static void clearAllSlots() {
  for (auto& s : s_conn) {
    s.handle = kUnusedHandle;
    s.authed = false;
    s.crypto = lamp::crypto::PerConnState{};
    s.pageSnapshot.clear();
    s.pageCursor = 0;
    s.pageMtu    = 0;
  }
}

static constexpr uint16_t TARGET_MTU = 512;

static bool isAuthed(uint16_t connHandle) {
  if (s_config->lamp.password.empty()) return true;  // No password: open access
  const ConnSlot* s = findSlot(connHandle);
  return s && s->authed;
}

// Parse a UUID string like "5f64f4d1-d6d9-4a44-9b3f-3a8d6f7e6b40" into 16
// bytes in reversed hex order so it matches the Dart side's
// `uuidSaltLE16(...)`. The HKDF salt is opaque: both sides just need to
// agree on the bytes.
static std::array<uint8_t, 16> uuidSaltLE(const char* uuid) {
  std::array<uint8_t, 16> out{};
  uint8_t bytes[16];
  size_t bi = 0;
  for (size_t i = 0; uuid[i] && bi < 16; ++i) {
    if (uuid[i] == '-') continue;
    auto hex2 = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + c - 'a';
      if (c >= 'A' && c <= 'F') return 10 + c - 'A';
      return 0;
    };
    int hi = hex2(uuid[i]);
    int lo = hex2(uuid[i + 1]);
    bytes[bi++] = static_cast<uint8_t>((hi << 4) | lo);
    ++i;  // consumed two hex chars
  }
  // Reverse to LE.
  for (size_t k = 0; k < 16; ++k) out[k] = bytes[15 - k];
  return out;
}

// Decode an inbound write payload. Dispatches on the magic-byte prefix:
//   - 0x02 → AES-GCM ciphertext via lamp::crypto::decryptOp.
//     Successful decrypt implicitly authenticates the connection
//     (GCM auth-tag has already verified the lamp password).
//   - 0x01 → explicit plaintext prefix; strip it and pass through.
//   - anything else (legacy unprefixed JSON, including the webapp's bare
//     '{' first byte) → treat the whole payload as plaintext JSON.
// Plaintext writes still require a prior CHAR_AUTH success to be authed.
// Returns true on success (json populated); false to silently reject.
static bool decodeIncomingOp(const std::string& raw,
                             uint16_t handle,
                             const uint8_t* charUuidLE16,
                             const char* charShortName,
                             std::string& outJson,
                             bool& authed) {
  const auto* p = reinterpret_cast<const uint8_t*>(raw.data());
  const size_t n = raw.size();
  if (n == 0) return false;

  if (lamp::crypto::magicByte(p, n) == lamp::crypto::MAGIC_CIPHERTEXT) {
    ConnSlot* slot = findOrAllocSlot(handle);
    if (!slot) return false;  // all slots taken (unreachable under NimBLE's conn cap)
    if (!lamp::crypto::decryptOp(p, n, charUuidLE16, charShortName,
                                 s_config->lamp.password, slot->crypto, outJson)) {
      return false;
    }
    slot->authed = true;  // GCM tag IS auth
    authed = true;
    return true;
  }

  // Plaintext path. `0x01` prefix is allowed and stripped; otherwise pass through.
  size_t start = (lamp::crypto::magicByte(p, n) == lamp::crypto::MAGIC_PLAINTEXT) ? 1 : 0;
  outJson.assign(reinterpret_cast<const char*>(p + start), n - start);
  authed = isAuthed(handle);
  return true;
}

class ControlServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    uint16_t handle = connInfo.getConnHandle();
    // Allocate the per-conn slot up front so plaintext auth attempts (which
    // assign authed=true on accept) have a place to land. If allocation fails
    // (all 3 slots taken), the connection simply stays unauthed; isAuthed()
    // returns false and all writes are rejected.
    findOrAllocSlot(handle);

    server->setDataLen(handle, 251);
    NimBLEDevice::setMTU(TARGET_MTU);

    // Request a tighter connection interval for live-preview throughput.
    // Android may decline (it weighs power saving), but when accepted this
    // widens the link from ~20 writes/sec at the default ~49ms interval to
    // ~33-66 writes/sec at 15-30ms, eliminating the queue backpressure that
    // made continuous slider drags lag.
    //   minInterval = 12 (15.0 ms), maxInterval = 24 (30.0 ms),
    //   latency = 0, supervision timeout = 400 (4.0 s).
    server->updateConnParams(handle, kTightMinUnits, kTightMaxUnits, 0,
                             kSupervisionTimeoutUnits);
    s_currentConnHandle = handle;

    s_scanPausedForGattClient = true;
    NimBLEDevice::getScan()->stop();

    // Track BT-session state. The lamp doesn't associate to WiFi
    // (presence-only home mode), so there's no STA to pause. wifi::tick skips
    // its background scans while this flag is set so the radio stays focused
    // on BT.
    s_clientConnected = true;
    s_homeModePageActive = false;
    // Recompute effective home mode now that the BT session is up: the gate
    // switches from presence-based to focus-based.
    postPendingApplyEffectiveBrightness();

#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] Client connected, handle=%u (BT session active)\n", handle);
#endif
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    uint16_t handle = connInfo.getConnHandle();
    freeSlot(handle);

    // Resume the central scan now that the phone is gone.
    s_scanPausedForGattClient = false;
    NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);

    s_clientConnected = false;
    s_homeModePageActive = false;
    // Clear all operator-editing flags so a stale flag from a crashed or
    // backgrounded app can't keep the wisp's overrides locked out for this
    // surface forever. Reconnected sessions re-open what they want via
    // CHAR_EDIT_SESSION.
    lamp::overrides.base.setOperatorEditing(false);
    lamp::overrides.shade.setOperatorEditing(false);
    lamp::overrides.brightness.setOperatorEditing(false);
    // Re-assert wisp paint into the configurators in case a mid-test BLE
    // write stomped the wisp's target gradient before disconnecting.
    lamp::overrides.base.reassertHold();
    lamp::overrides.shade.reassertHold();
    s_currentConnHandle = 0xFFFF;
    postPendingApplyEffectiveBrightness();
    // Phone walked away: force-commit any pending disposition writes so the
    // user's final slider value survives a power loss before the 5s debounce
    // window elapses. No-op if not dirty (the common case for disconnects
    // unrelated to the social tab). The flush runs on Core 1 from the loop
    // drain; this just posts a flag.
    postPendingFlushDispositions();
    // Force-flush a pending commit on disconnect so a quick edit-then-
    // disconnect doesn't lose the user's last change. The loop drain
    // sees g_forceCommitFlush and flushes immediately (skips the idle
    // window).
    lamp::pendingSlots.forceCommitFlush = true;

#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] Client disconnected, handle=%u reason=%d\n", handle, reason);
#endif
  }
};

class AuthCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    static const auto uuid = uuidSaltLE(CHAR_AUTH);
    const uint16_t handle = connInfo.getConnHandle();
    const std::string raw = c->getValue();
    if (raw.size() > 256) return;  // password length sanity
    std::string body;
    bool authed = false;
    if (!decodeIncomingOp(raw, handle, uuid.data(), "auth", body, authed)) return;
    if (authed) {
      // Ciphertext path already marked the slot authed; nothing more to do.
#ifdef LAMP_DEBUG
      Serial.printf("[ble_control] Auth via ciphertext handle=%u OK\n", handle);
#endif
      return;
    }
    // Plaintext path: compare the decoded body against the lamp password.
    const bool accepted = (body == s_config->lamp.password);
    if (auto* slot = findOrAllocSlot(handle)) {
      slot->authed = accepted;
    }
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] Auth attempt handle=%u %s\n",
                  handle, accepted ? "ACCEPTED" : "REJECTED");
#endif
  }
};

// Brightness: write-without-response, single u8 0-100.
class BrightnessCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.empty()) return;
    uint8_t level = static_cast<uint8_t>(val[0]);
    if (level > 100) level = 100;
#ifdef LAMP_DEBUG
    // Pair with [drain] brightness t_us=... in lamp.cpp to measure
    // BLE->loop latency under continuous slider drag.
    Serial.printf("[ble] brightness recv level=%u t_us=%lu\n",
                  level, (unsigned long)micros());
#endif
    // Zero-alloc on Core 0. The drain in lamp.cpp::loop() reads
    // pendingBrightness on Core 1, updates config + timestamps + strip
    // brightness all on Core 1.
    postPendingBrightness(static_cast<int8_t>(level & 0x7F));
  }
};

// Shade colors / Base colors: plaintext JSON arrays of hex strings.
// Routed through WriteRouter; instantiated below in start().

// Base knockout: write-without-response, 2 bytes [pixelIndex u8, brightness% u8].

// Edit-session callback: operator-priority lockout for wisp overrides.
// 2-byte payload [surface, state]; see CHAR_EDIT_SESSION in the .hpp for
// the bitmask values.
class EditSessionCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.size() < 2) return;
    const uint8_t surface = static_cast<uint8_t>(val[0]);
    const bool   open    = (val[1] != 0);
    if (surface & 0x01) lamp::overrides.base.setOperatorEditing(open);
    if (surface & 0x02) lamp::overrides.shade.setOperatorEditing(open);
    if (surface & 0x04) lamp::overrides.brightness.setOperatorEditing(open);
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] editSession surface=0x%02x %s\n",
                  surface, open ? "open" : "closed");
#endif
  }
};

class HomeModeFocusCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.empty()) return;
    const bool active = val[0] != 0;
    s_homeModePageActive = active;
    postPendingApplyEffectiveBrightness();
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] HOME_MODE_FOCUS %s\n", active ? "on" : "off");
#endif
  }
};

class BaseKnockoutCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.size() < 2) return;
    uint8_t pixelIndex = static_cast<uint8_t>(val[0]);
    uint8_t brightness = static_cast<uint8_t>(val[1]);
    if (brightness > 100) brightness = 100;
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] WRITE knockout pixel=%u brightness=%u\n", pixelIndex, brightness);
#endif
    postPendingKnockout(pixelIndex, brightness);
  }
};

// ExpressionOp (plaintext JSON), WifiOp + RemoteOp (AES-GCM ciphertext) are
// routed through WriteRouter; instantiated in start() below.

// Nearby lamps: build the unified per-transport list. Tags each entry with
// viaBle / viaEspNow flags reflecting whether the transport has carried at
// least one sighting in this entry's current lifetime. The list itself
// prunes after LAMP_PRUNE_TIME_MS, so stale entries disappear rather than
// going badge-less; a separate per-flag recency window would make rows
// appear with no badges between the recency cutoff and the prune cutoff
// (the "ghost row" bug).
//
// No size cap: the page protocol (CTRL+DATA chunking) streams arbitrarily
// long sections via successive 244 B chunks, so MTU is not the constraint.
// The natural ceiling is NearbyLamps::MAX_NEARBY = 32 peers, ~200 B per
// entry, ~6.4 KB total: well under the page protocol's uint16_t cursor
// (~65 KB) and a trivial fraction of heap.
static std::string buildNearbyLampsJson() {
  auto lamps = lamp::nearbyLamps.getAll();
  // Sort by name for stable rendering.
  std::sort(lamps.begin(), lamps.end(),
            [](const lamp::NearbyLamp& a, const lamp::NearbyLamp& b) {
              return a.name < b.name;
            });
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& p : lamps) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = p.name;
    // Use the freshest transport's timestamp as a single "lastSeen" for the
    // app's display sort / age cue.
    o["lastSeenMs"] = (p.lastSeenViaEspNowMs > p.lastSeenViaBleMs)
                          ? p.lastSeenViaEspNowMs
                          : p.lastSeenViaBleMs;
    o["viaBle"]    = (p.lastSeenViaBleMs    != 0);
    o["viaEspNow"] = (p.lastSeenViaEspNowMs != 0);
    if (p.hasMac) {
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               p.mac[0], p.mac[1], p.mac[2], p.mac[3], p.mac[4], p.mac[5]);
      o["mac"] = macStr;
    }
    if (!p.bdAddr.empty()) {
      o["bdAddr"] = p.bdAddr;
    }
    if (p.lastRssi != -127) {
      o["rssi"] = p.lastRssi;
      o["proximity"] = lamp::proximityToInt(lamp::proximityFor(p.lastRssi));
    }
    JsonArray sh = o["shade"].to<JsonArray>();
    sh.add(p.shadeColor.r); sh.add(p.shadeColor.g); sh.add(p.shadeColor.b); sh.add(p.shadeColor.w);
    JsonArray ba = o["base"].to<JsonArray>();
    ba.add(p.baseColor.r);  ba.add(p.baseColor.g);  ba.add(p.baseColor.b);  ba.add(p.baseColor.w);
    // Mesh-debug fields: firmware version (packed semver) + current
    // OTA state (0=idle, 1=sending, 2=receiving) as seen in the last
    // HELLO from this peer. Both omitted when zero/idle to keep the
    // JSON compact in the common case (most peers most of the time).
    if (p.firmwareVersion != 0) o["fwVersion"] = p.firmwareVersion;
    if (p.otaState != 0) o["otaState"] = p.otaState;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

// Social dispositions: read returns the full per-peer map; write replaces
// it. Auth-gated both directions: even though the disposition values aren't
// credentials, exposing the friendship map to an unauthenticated scanner
// would leak the lamp's peer relationships.
class SocialDispositionsCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) {
      c->setValue("");
      return;
    }
    c->setValue(s_config->asDispositionsJson().c_str());
  }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.size() > lamp::kPendingJsonOp) return;
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] WRITE socialDispositions len=%u\n",
                  (unsigned)val.size());
#endif
    // Memcpy-only on Core 0; loop task drains + parses + persists so the
    // NVS write serialises against the settings_blob drain on Core 1
    // (shared `prefs` instance can't tolerate concurrent begin/end).
    postPendingSocialDispositionsJson(val.data(), val.size());
  }
};

// Wisp status: read+notify the cached wispStatus JSON merged with the last
// MSG_WISP_HELLO payload. Auth-gated: a wisp's runtime state (current zone,
// palette progress, etc.) shouldn't be readable to an unauthenticated
// scanner that knows the service UUID.
//
// Build path lives entirely inside NearbyLamps::getWispStatusReadJson so
// both the on-read callback and the notify helper hand back the same bytes.
// Returns "{}" when nothing has been cached yet; the app treats
// empty/object-only payloads as "no wisp on this mesh yet".
class WispStatusCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) {
      c->setValue("");
      return;
    }
    c->setValue(lamp::nearbyLamps.getWispStatusReadJson(true));
  }
};

void notifyWispStatus() {
  if (!s_wispStatusChar) return;
  auto json = lamp::nearbyLamps.getWispStatusReadJson(false);
#ifdef LAMP_DEBUG
  // Diagnostic: print the JSON length and the controllingBase/controllingShade
  // values so it's clear whether the lamp reports them true at notify-out
  // time (firmware state machine) versus the app failing to render the icon.
  const bool sawCB = json.find("\"controllingBase\":true") != std::string::npos;
  const bool sawCS = json.find("\"controllingShade\":true") != std::string::npos;
  Serial.printf("[wisp_state] notify len=%u controllingBase=%d controllingShade=%d preview=%.220s\n",
                (unsigned)json.length(), sawCB ? 1 : 0, sawCS ? 1 : 0,
                json.c_str());
#endif
  s_wispStatusChar->setValue(json);
  s_wispStatusChar->notify();
}

static const char* wifiStateName(wifi::State s) {
  switch (s) {
    case wifi::IDLE:       return "idle";
    case wifi::SCANNING:   return "scanning";
    case wifi::FAILED:     return "failed";
  }
  return "unknown";
}

static std::string buildWifiStateJson(bool includeScanResults) {
  JsonDocument doc;
  doc["state"] = wifiStateName(wifi::state());
  // No "ssid" / "ip": the lamp never associates in presence-only mode. App
  // reads home.ssid from CHAR_HOME_SECTION; "is the lamp currently in home
  // mode" is implicit in the BT-session UX.
  if (!wifi::lastError().empty()) {
    doc["lastError"] = wifi::lastError();
  }
  if (includeScanResults) {
    auto results = wifi::consumeScanResults();
    if (!results.empty()) {
      // Sort strongest first so the trimmed list keeps the most useful entries.
      std::sort(results.begin(), results.end(),
                [](const wifi::ScanResult& a, const wifi::ScanResult& b) {
                  return a.rssi > b.rssi;
                });
      // Drop duplicate SSIDs (multiple BSSIDs per network are common).
      std::unordered_set<std::string> seen;
      JsonArray arr = doc["scanResults"].to<JsonArray>();
      // BLE notify payload is capped at MTU-3 (~509 B). Stop adding entries
      // once the serialized doc would exceed a soft budget so the notify
      // doesn't get rejected by NimBLE with `val > max`.
      constexpr size_t SOFT_BUDGET = 480;
      for (const auto& r : results) {
        if (r.ssid.empty()) continue;
        if (!seen.insert(r.ssid).second) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = r.ssid;
        o["rssi"] = r.rssi;
        o["encrypted"] = r.encrypted;
        if (measureJson(doc) > SOFT_BUDGET) {
          arr.remove(arr.size() - 1);
          break;
        }
      }
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

class WifiStateCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    auto json = buildWifiStateJson(true);
    c->setValue(json);
  }
};

void notifyWifiState() {
  if (!s_wifiStateChar) return;
  auto json = buildWifiStateJson(true);
  s_wifiStateChar->setValue(json);
  s_wifiStateChar->notify();
}

// ExpressionTest (plaintext) is routed through WriteRouter. Unique flag:
// empty-payload writes (the "test complete" sentinel) MUST reach the drain,
// so the router is constructed with allowEmpty(true).

class SettingsBlobCallback : public NimBLECharacteristicCallbacks {
  // Write-only path. App reads sections via the page protocol
  // (CHAR_PAGE_CTRL + CHAR_PAGE_DATA); CHAR_SETTINGS_BLOB is the
  // write-and-reboot save target only.
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (lamp::ota_quiet_mode::isQuiet()) return;
    static const auto uuid = uuidSaltLE(CHAR_SETTINGS_BLOB);
    const uint16_t handle = connInfo.getConnHandle();
    const std::string raw = c->getValue();
    // Settings blob can be larger than the op bound; add 64 for ciphertext overhead.
    // NimBLE already enforces MTU on the link; this is a sanity ceiling only.
    if (raw.size() > 4096 + 64) return;
    std::string json;
    bool authed = false;
    if (!decodeIncomingOp(raw, handle, uuid.data(), "settingsBlob", json, authed)) {
#ifdef LAMP_DEBUG
      // Diagnostic: dump password length (not content) and wire magic byte
      // to reveal a diverged lamp/app password state. A common cause of
      // "decrypt/decode failed" on a settings_blob write is the app
      // encrypting with a non-empty cached password against a lamp whose NVS
      // password is empty: decryptOp short-circuits at password.empty() and
      // returns false.
      const uint8_t firstByte = raw.empty() ? 0 : (uint8_t)raw[0];
      Serial.printf("[ble_control] settings_blob write: decrypt/decode failed "
                    "(lamp_pw_len=%u, wire_magic=0x%02x, wire_len=%u)\n",
                    (unsigned)s_config->lamp.password.size(),
                    (unsigned)firstByte,
                    (unsigned)raw.size());
#endif
      return;
    }
    if (!authed) {
#ifdef LAMP_DEBUG
      Serial.printf("[ble_control] settings_blob write: not authed\n");
#endif
      return;
    }
    if (json.empty()) return;

#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] WRITE settingsBlob len=%u (decoded)\n", (unsigned)json.size());
#endif

    // Hand off to Core 1's loop drain. Runs AFTER expressionOp drain so any
    // just-arrived expression edits are mirrored into config.expressions
    // before settings_blob serializes + persists. See lamp.cpp's
    // settings_blob drain block.
    if (json.size() > lamp::kPendingJsonOp) {
#ifdef LAMP_DEBUG
      Serial.printf("[ble_control] settings_blob too large for pending slot: %u > %u\n",
                    (unsigned)json.size(), (unsigned)lamp::kPendingJsonOp);
#endif
      return;
    }
    postPendingSettingsBlobJson(json.data(), json.size());
  }
};

// Page protocol: paginated lamp→app section reads.
//
// A section's full JSON can exceed the BLE 4.x ATT ceiling of 512 bytes:
// the vendored ble_att.h `#define BLE_ATT_ATTR_MAX_LEN 512` overrides the
// `-D BLE_ATT_ATTR_MAX_LEN=1024` build flag because it has no `#ifndef`
// guard. A 3-expression lamp serialises its expressions section to 579
// bytes; setValue() rejects it at boot and the characteristic comes up
// empty.
//
// The CTRL+DATA pair sidesteps the cap by streaming MTU-sized chunks from a
// per-connection snapshot. The wire mechanic relies on NimBLE's onRead
// firing once per app-level GATT_READ_REQ (not once per ATT PDU: the host
// continues long values via ATT_READ_BLOB_REQ against the cached AttValue
// without re-firing onRead). Seeding the AttValue with <= kPageMaxChunkSize
// bytes leaves the host no continuation; the next app read() re-fires
// onRead and the cursor advances.
//
// CHAR_WISP_STATUS keeps its own read+notify path: the notify channel is
// live (lamp.cpp calls notifyWispStatus()) and its payload fits under 512.

using SectionSerializer = void(*)(std::string&);

struct SectionEntry {
  const char*       name;
  SectionSerializer fn;
};

// Six paginatable sections. Lambdas capture only globals → decay to
// plain function pointers, no std::function footprint. ble_control::tick
// proactively rebuilds dirty section caches on Core 1, so the steady-
// state CTRL onWrite body is just a string copy on Core 0; the portMUX
// inside Config::*SectionJsonCached() covers the race if a CTRL-write
// arrives before tick fires after an invalidate.
static const std::array<SectionEntry, 6> kSections = {{
  {"lamp",   [](std::string& out) { out = s_config->lampSectionJsonCached(); }},
  {"base",   [](std::string& out) { out = s_config->baseSectionJsonCached(); }},
  {"shade",  [](std::string& out) { out = s_config->shadeSectionJsonCached(); }},
  {"expr",   [](std::string& out) { out = s_config->expressionsSectionJsonCached(); }},
  {"home",   [](std::string& out) { out = s_config->homeSectionJsonCached(); }},
  {"nearby", [](std::string& out) { out = buildNearbyLampsJson(); }},
}};

class PageCtrlCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    const uint16_t handle = connInfo.getConnHandle();
    if (!isAuthed(handle)) return;
    const std::string name = c->getValue();
    if (name.empty() || name.size() > 16) return;

    ConnSlot* slot = findSlot(handle);
    if (!slot) return;

    for (const auto& entry : kSections) {
      if (name == entry.name) {
        // Reuse the std::string's capacity across sections so the heap sees
        // growth, not churn. Capture the negotiated MTU at snapshot time so
        // the chunk size is stable for this read sweep even if conn-params
        // shift mid-stream.
        entry.fn(slot->pageSnapshot);
        slot->pageCursor = 0;
        // connInfo.getMTU() returns the negotiated ATT MTU. Floor it against
        // the hardcoded wire constant so the app's "short = done" heuristic
        // stays correct even on peers that negotiate higher than 247.
        const uint16_t mtu = connInfo.getMTU();
        const uint16_t cap = mtu > 3 ? mtu - 3 : 0;
        slot->pageMtu = (cap > 0 && cap < kPageMaxChunkSize) ? cap
                                                              : kPageMaxChunkSize;
#ifdef LAMP_DEBUG
        Serial.printf("[ble_control] page CTRL section=%s len=%u mtu=%u chunk=%u\n",
                      entry.name, (unsigned)slot->pageSnapshot.size(),
                      (unsigned)mtu, (unsigned)slot->pageMtu);
#endif
        return;
      }
    }

    // Unknown section name: clear any prior snapshot so the next DATA read
    // returns empty (the app interprets that as "section not found").
    slot->pageSnapshot.clear();
    slot->pageCursor = 0;
    slot->pageMtu    = 0;
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] page CTRL unknown section='%.*s'\n",
                  (int)name.size(), name.data());
#endif
  }
};

class PageDataCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    const uint16_t handle = connInfo.getConnHandle();
    if (!isAuthed(handle)) {
      c->setValue("");
      return;
    }
    ConnSlot* slot = findSlot(handle);
    if (!slot || slot->pageMtu == 0 ||
        slot->pageCursor >= slot->pageSnapshot.size()) {
      // No active page session, or cursor already past the end. Empty
      // response: the app's "short chunk = done" heuristic interprets
      // 0 bytes as the end.
      c->setValue("");
      return;
    }
    const size_t remaining = slot->pageSnapshot.size() - slot->pageCursor;
    const size_t take      = remaining < slot->pageMtu ? remaining
                                                       : slot->pageMtu;
    c->setValue(reinterpret_cast<const uint8_t*>(slot->pageSnapshot.data() +
                                                  slot->pageCursor),
                static_cast<size_t>(take));
    slot->pageCursor += static_cast<uint16_t>(take);
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] page DATA cursor=%u/%u take=%u\n",
                  (unsigned)slot->pageCursor,
                  (unsigned)slot->pageSnapshot.size(),
                  (unsigned)take);
#endif
  }
};

// Firmware OTA characteristics: CHAR_FW_CONTROL (write+notify) carries the
// MSG_FW_* control frames, CHAR_FW_CHUNK (write) carries chunk payloads.
// Wire format: docs/dev/mesh-api.md.

static NimBLECharacteristic*  s_fwControlChar = nullptr;
static lamp::FirmwareReceiver* s_firmwareReceiver = nullptr;

class BleFirmwareTransport : public lamp::FirmwareTransport {
 public:
  void getMyMac(uint8_t out[6]) const override {
    // Same chip MAC as ESP-NOW on ESP32 (BT controller and Wi-Fi share the
    // OUI; as a sourceMac identifier either is fine, both stable across
    // reboots).
    const ble_addr_t* a = NimBLEDevice::getAddress().getBase();
    if (a) std::memcpy(out, a->val, 6);
    else   std::memset(out, 0, 6);
  }
  bool sendFrame(const uint8_t* data, size_t len) override {
    if (!s_fwControlChar) return false;
    s_fwControlChar->setValue(data, len);
    s_fwControlChar->notify();
    return true;
  }
};

static BleFirmwareTransport s_bleFwTransport;

class FwControlCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    const std::string raw = c->getValue();
    if (raw.size() < lamp_protocol::HEADER_SIZE) return;
    const uint8_t msgType = lamp_protocol::inspect(
        reinterpret_cast<const uint8_t*>(raw.data()), raw.size());

    lamp::PendingFirmwareControl slot{};
    slot.transportKind  = lamp::FirmwareTransportKind::Ble;
    slot.bleConnHandle  = connInfo.getConnHandle();
    slot.wireVersion    = static_cast<uint8_t>(raw.size() >= 3 ? raw[2] : 0);

    if (msgType == lamp_protocol::MSG_FW_OFFER) {
      lamp_protocol::ParsedFwOffer p;
      if (!lamp_protocol::parseFwOffer(
              reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), p)) {
        return;
      }
      slot.msgType = lamp_protocol::MSG_FW_OFFER;
      slot.seq     = p.seq;
      std::memcpy(slot.sourceMac, p.sourceMac, 6);
      std::memcpy(slot.targetMac, p.targetMac, 6);
      slot.offer.version       = p.version;
      slot.offer.totalLen      = p.totalLen;
      slot.offer.chunkSize     = p.chunkSize;
      std::memcpy(slot.offer.channel, p.channel,
                  lamp_protocol::FW_CHANNEL_LEN);
      std::memcpy(slot.offer.sha256Prefix, p.sha256Prefix,
                  lamp_protocol::FW_SHA256_PREFIX_LEN);
      slot.offer.footerLen   = p.footerLen;
      slot.offer.totalChunks = p.totalChunks;
      lamp::postPendingFirmwareControl(slot);
    } else if (msgType == lamp_protocol::MSG_FW_DONE) {
      lamp_protocol::ParsedFwDone p;
      if (!lamp_protocol::parseFwDone(
              reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), p)) {
        return;
      }
      slot.msgType = lamp_protocol::MSG_FW_DONE;
      slot.seq     = p.seq;
      std::memcpy(slot.sourceMac, p.sourceMac, 6);
      std::memcpy(slot.targetMac, p.targetMac, 6);
      slot.done.version  = p.version;
      slot.done.totalLen = p.totalLen;
      std::memcpy(slot.done.sha256Prefix, p.sha256Prefix,
                  lamp_protocol::FW_SHA256_PREFIX_LEN);
      slot.done.footerLen = p.footerLen;
      lamp::postPendingFirmwareControl(slot);
    }
    // Other MSG_FW_* types (CHUNK/ACCEPT/REQ/RESULT) don't belong on this
    // char; silently drop. MSG_FW_CHUNK goes to CHAR_FW_CHUNK below.
  }
};

class FwChunkCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (!s_firmwareReceiver) return;
    const std::string raw = c->getValue();
    lamp_protocol::ParsedFwChunk p;
    if (!lamp_protocol::parseFwChunk(
            reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), p)) {
      return;
    }
    // Direct fast-path call on the BLE host task. The receiver's handler is
    // bounded (~0.5 ms: one OTA partition write + bitmap set). Dropping a
    // chunk here just means the receiver's stall watchdog REQs it next tick.
    s_firmwareReceiver->handleChunkOnRecvTask(p);
  }
};

// Per-loop housekeeping on Core 1.
void tick() {
  if (!s_running || !s_config) return;

  // Proactively rebuild any dirty section caches on Core 1 so the BLE
  // host task (Core 0) page-protocol read finds them already populated
  // and never has to serialize JSON inside the NimBLE callback. The
  // Cached() accessors return immediately when not dirty (cheap flag
  // check), and the portMUX inside makes the rebuild safe if Core 0
  // races in via a CTRL-write before tick fires.
  s_config->lampSectionJsonCached();
  s_config->baseSectionJsonCached();
  s_config->shadeSectionJsonCached();
  s_config->expressionsSectionJsonCached();
  s_config->homeSectionJsonCached();
}

void notifyStateChange() {
  if (!s_stateNotify) return;
  // previewActive is the firmware-truth bit for the app's Test button.
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"previewActive\":%s}",
           ::expressionManager.isAnyTestActive() ? "true" : "false");
  s_stateNotify->setValue(buf);
  s_stateNotify->notify();
}

void start(lamp::Config* config) {
  if (s_running) return;

  s_config = config;

  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init(config->lamp.name.substr(0, 12));
  }

  // NimBLE supports only one server per device.  createServer() returns the
  // existing instance on subsequent calls, so this is safe to call even if
  // ble_setup already created a server.
  s_server = NimBLEDevice::createServer();
  // Pass deleteCallbacks=false: the callbacks object lives for the process
  // lifetime and must not be freed by NimBLE.
  s_server->setCallbacks(new ControlServerCallbacks(), false);

  // NimBLE 2.x default is FALSE: advertising does NOT auto-restart after a
  // client disconnects (per CHANGELOG). Without this, the first phone disconnect
  // makes the lamp permanently undiscoverable until reboot.
  s_server->advertiseOnDisconnect(true);

  s_service = s_server->createService(SERVICE_UUID);

  // Auth: write-with-response so the app receives a GATT ack. App-layer
  // crypto: AES-GCM (0x02 prefix) auto-authenticates via the GCM tag;
  // plaintext writes compare against lamp.password. No link-layer bonding.
  s_service->createCharacteristic(CHAR_AUTH,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new AuthCallback());

  // Live-preview characteristics: slider-rate writes. Declare BOTH WRITE and
  // WRITE_NR so the client can choose write-without-response (the app does,
  // via fbp_ble_client.write(withoutResponse: true)). WRITE alone forces a
  // GATT ACK round trip per write, which capped throughput at ~5 Hz on the
  // test phone at the ~49ms connection interval (visibly laggy slider drag).
  static constexpr uint32_t LIVE_WRITE_PROPS =
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;

  s_service->createCharacteristic(CHAR_BRIGHTNESS, LIVE_WRITE_PROPS)
      ->setCallbacks(new BrightnessCallback());
  // Plaintext live-preview JSON chars via WriteRouter. Each instance captures
  // its own (slot cap, post helper, debug tag).
  s_service->createCharacteristic(CHAR_SHADE_COLORS, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonBase, postPendingShadeColorsJson, isAuthed))
              ->setDebugTag("shadeColors"));
  s_service->createCharacteristic(CHAR_BASE_COLORS, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonBase, postPendingBaseColorsJson, isAuthed))
              ->setDebugTag("baseColors"));
  // CHAR_COMMIT: parameterless commit signal. Plaintext WriteRouter gated by
  // isAuthed(). allowEmpty=true so a single sentinel byte OR an empty payload
  // both count as the commit signal (bytes ignored, arrival IS the signal).
  //
  // Properties = WRITE | WRITE_NR. The app uses WRITE_NR (commit() calls
  // ble.write(..., withoutResponse: true)) so the BLE FIFO doesn't stall on
  // the ACK round-trip during the NVS persist; commit-success is delivered
  // via the CHAR_STATE_NOTIFY {"commit":"ok"|"err"} payload instead. WRITE
  // stays on the mask so older app builds that issue WRITE-with-response
  // don't fail the GATT property check.
  s_service->createCharacteristic(CHAR_COMMIT_UUID, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          /*maxSize=*/4, postPendingCommit, isAuthed))
              ->setDebugTag("commit")
              ->setAllowEmpty(true));
  s_service->createCharacteristic(CHAR_BASE_KNOCKOUT, LIVE_WRITE_PROPS)
      ->setCallbacks(new BaseKnockoutCallback());
  // Home-mode focus: app signals whether the user is on the Home Mode
  // setup page. See HomeModeFocusCallback above.
  s_service->createCharacteristic(CHAR_HOME_MODE_FOCUS, LIVE_WRITE_PROPS)
      ->setCallbacks(new HomeModeFocusCallback());
  // Operator-priority lockout signal; see EditSessionCallback above.
  s_service->createCharacteristic(CHAR_EDIT_SESSION, LIVE_WRITE_PROPS)
      ->setCallbacks(new EditSessionCallback());
  // CHAR_EXPRESSION_TEST: live-preview semantics, best-effort writes that
  // don't need a delivery guarantee. Exposes WRITE | WRITE_NR so the app can
  // pick withoutResponse to bypass the per-write ACK round-trip (which under
  // wide conn params + heavy notify load can stall the FBP queue). Empty
  // payload is the "test complete" sentinel and MUST reach the loop drain:
  // allowEmpty(true) on the router.
  s_service->createCharacteristic(CHAR_EXPRESSION_TEST, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingTestActionJson, isAuthed))
              ->setDebugTag("expressionTest")
              ->setAllowEmpty(true));
  s_service->createCharacteristic(CHAR_EXPRESSION_OP, NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingExpressionOpJson, isAuthed))
              ->setDebugTag("expressionOp"));
  // WiFi op: AES-GCM ciphertext. Salt array lives function-local-static so
  // the router can hold a stable pointer for the life of the service.
  static const auto kWifiOpSalt = uuidSaltLE(CHAR_WIFI_OP);
  s_service->createCharacteristic(CHAR_WIFI_OP,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingWifiOpJson, isAuthed,
          decodeIncomingOp, kWifiOpSalt.data(), "wifiOp"))
              ->setDebugTag("wifiOp"));
  s_wifiStateChar = s_service->createCharacteristic(
      CHAR_WIFI_STATE,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_wifiStateChar->setCallbacks(new WifiStateCallback());
  s_wifiStateChar->setValue(buildWifiStateJson(false));

  // Page protocol: paginated lamp→app section reads. CHAR_PAGE_CTRL onWrite
  // snapshots the named section into the connection's slot + resets a cursor;
  // CHAR_PAGE_DATA onRead returns the next chunk. App reads CHAR_PAGE_DATA
  // until a short chunk (< kPageMaxChunkSize) lands. See the block comment
  // above PageCtrlCallback for the why.
  s_service->createCharacteristic(CHAR_PAGE_CTRL, NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new PageCtrlCallback());
  s_service->createCharacteristic(CHAR_PAGE_DATA, NIMBLE_PROPERTY::READ)
      ->setCallbacks(new PageDataCallback());

  // Social dispositions: read + write, both auth-gated. Initial seed shows
  // whatever was loaded from NVS at boot.
  s_service->createCharacteristic(
      CHAR_SOCIAL_DISPOSITIONS,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new SocialDispositionsCallback());

  // Remote op: AES-GCM ciphertext, same shape as wifi op.
  static const auto kRemoteOpSalt = uuidSaltLE(CHAR_REMOTE_OP);
  s_service->createCharacteristic(CHAR_REMOTE_OP,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingRemoteOpJson, isAuthed,
          decodeIncomingOp, kRemoteOpSalt.data(), "remoteOp"))
              ->setDebugTag("remoteOp"));

  // Wisp op: plaintext JSON. The wispOp wire format is open-set
  // ({"char":"wispOp","op":"setZone","zoneId":N}) and the wisp owns the
  // vocabulary; encrypting it would force a lamp firmware bump every time the
  // wisp gains a new op. App-layer auth still gates writes via isAuthed().
  s_service->createCharacteristic(CHAR_WISP_OP, NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingWispOpJson, isAuthed))
              ->setDebugTag("wispOp"));

  // Wisp status: read + notify. Build JSON from the cached wispStatus payload
  // merged with the last MSG_WISP_HELLO data. Notify fires whenever the loop
  // drain caches a new wispStatus (see lamp.cpp::loop).
  s_wispStatusChar = s_service->createCharacteristic(
      CHAR_WISP_STATUS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_wispStatusChar->setCallbacks(new WispStatusCallback());
  s_wispStatusChar->setValue(lamp::nearbyLamps.getWispStatusReadJson());

  // Settings blob: write-only. Reads go through the page protocol. App-layer
  // crypto: the full config save is AES-GCM encrypted.
  s_service->createCharacteristic(CHAR_SETTINGS_BLOB,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new SettingsBlobCallback());

  // Firmware OTA: write + notify on CHAR_FW_CONTROL for the
  // OFFER/DONE/ACCEPT/REQ/RESULT control plane; write-without-response on
  // CHAR_FW_CHUNK for the high-frequency 200-byte chunk stream.
  s_fwControlChar = s_service->createCharacteristic(
      CHAR_FW_CONTROL,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_fwControlChar->setCallbacks(new FwControlCallback());
  s_service->createCharacteristic(CHAR_FW_CHUNK, LIVE_WRITE_PROPS)
      ->setCallbacks(new FwChunkCallback());

  // State notify: notify only; no write/read needed
  s_stateNotify = s_service->createCharacteristic(CHAR_STATE_NOTIFY,
                                                  NIMBLE_PROPERTY::NOTIFY);
  s_stateNotify->setValue("{}");

  // Schema version: read-only, tail-appended. The app reads this to detect
  // the lamp's attribute-layout version; lamps predating it read as absent
  // and the app falls back to legacy behavior. Appending at the tail keeps
  // every existing handle in place, so deployed app installs are unaffected.
  // See gatt_layout.hpp + the frozen-layout lock-in in CLAUDE.md.
  static const uint8_t kSchemaVersionValue = kGattSchemaVersion;
  s_service->createCharacteristic(CHAR_SCHEMA_VERSION, NIMBLE_PROPERTY::READ)
      ->setValue(&kSchemaVersionValue, 1);

  s_service->start();

  // Bind the frozen layout table to the live registration. A mismatch means a
  // characteristic was added/removed/reordered without updating gatt_layout.hpp
  // (and bumping kGattSchemaVersion), which would silently stale-out paired
  // app installs. Loud but non-fatal: never brick a deployed lamp over a
  // dev-time invariant.
  {
    const auto& liveChars = s_service->getCharacteristics();
    bool layoutOk = liveChars.size() == kGattLayoutCount;
    for (size_t i = 0; layoutOk && i < kGattLayoutCount; ++i) {
      if (!liveChars[i]->getUUID().equals(
              NimBLEUUID(std::string(kGattLayout[i].uuid)))) {
        layoutOk = false;
      }
    }
    if (!layoutOk) {
      Serial.printf(
          "[ble_control] GATT LAYOUT DRIFT: live registration != "
          "gatt_layout.hpp (expected %u chars). Update the table + bump "
          "kGattSchemaVersion.\n",
          static_cast<unsigned>(kGattLayoutCount));
    }
  }

  // Don't touch advertising: BluetoothComponent::begin() already configures
  // the advertiser as connectable (BLE_GAP_CONN_MODE_UND) with the color-sync
  // manufacturer data the app scans for. The control GATT service attaches to
  // the GATT server and is discovered AFTER connection, not advertised in the
  // packet (a 128-bit service UUID would overflow the 31-byte adv limit).

  s_running = true;

#ifdef LAMP_DEBUG
  Serial.printf("[ble_control] GATT control service started\n");
#endif
}

void stop() {
  if (!s_running) return;

  NimBLEDevice::getAdvertising()->stop();

  if (s_server && s_service) {
    s_server->removeService(s_service, true);
    s_service        = nullptr;
    s_stateNotify    = nullptr;
    s_wifiStateChar  = nullptr;
    s_wispStatusChar = nullptr;
  }

  s_server  = nullptr;
  s_running = false;
  clearAllSlots();

#ifdef LAMP_DEBUG
  Serial.printf("[ble_control] GATT control service stopped\n");
#endif
}

bool isRunning() { return s_running; }

static volatile bool s_otaRadioPaused = false;

void pauseRadioForOta() {
  if (s_otaRadioPaused) return;
  s_otaRadioPaused = true;
  // Stop both adv and scan so the IDF coex arbiter stops gating WiFi RX
  // during BLE windows. Phone app loses connection briefly; reconnects
  // on resumeRadioAfterOta or after the OTA-driven reboot.
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::getScan()->stop();
  // Also tear down the boot-window SoftAP webapp. AP beacons fire every
  // ~100 ms and contend with the ESP-NOW chunk stream for WiFi airtime;
  // shutting the AP off frees the band for the OTA. The webapp is a
  // first-boot config UI that tears itself down after the 120 s boot window
  // regardless, so this just brings the inevitable forward.
#if LAMP_WEBAPP_ENABLED
  webapp::shutdownForOta();
#endif
#ifdef LAMP_DEBUG
  Serial.println("[ble_control] paused adv + scan + softAP for OTA");
#endif
}

// Kick any active GATT client. The GATT server stays up; only the connection
// is killed. Safe to call from any task: NimBLEServer::disconnect serialises
// via the NimBLE host task's event queue. Idempotent.
void disconnectGattClientsForOta() {
  if (s_currentConnHandle == 0xFFFF) return;
  if (!s_server) return;
  s_server->disconnect(s_currentConnHandle);
#ifdef LAMP_DEBUG
  Serial.printf("[ble_control] kicked GATT client handle=%u for OTA\n",
                s_currentConnHandle);
#endif
}

void resumeRadioAfterOta() {
  if (!s_otaRadioPaused) return;
  s_otaRadioPaused = false;
  // Only restart scan if the GATT-client pause path doesn't have it
  // suppressed (a connected phone holds scan paused independently).
  if (!s_scanPausedForGattClient) {
    NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);
  }
  NimBLEDevice::getAdvertising()->start();
#ifdef LAMP_DEBUG
  Serial.println("[ble_control] resumed adv + scan after OTA");
#endif
}

// Bind the BLE transport so CHAR_FW_CONTROL notifications route back
// correctly. Called from lamp.cpp::setup() after firmwareReceiver.begin().
void setFirmwareReceiver(lamp::FirmwareReceiver* receiver) {
  s_firmwareReceiver = receiver;
  if (receiver) {
    receiver->setBleTransport(&s_bleFwTransport);
  }
}

// Signature matches WriteRouter::PostFn; data/len are semantically ignored,
// the arrival of the write IS the commit signal.
void postPendingCommit(const char* /*data*/, size_t /*len*/) {
  // Single-bool naturally atomic on Xtensa; no portMUX.
  lamp::pendingSlots.pendingCommit = true;
}

}  // namespace ble_control
