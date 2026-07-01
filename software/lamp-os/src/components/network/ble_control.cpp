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
#include "behaviors/fade_out.hpp"
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "components/webapp/webapp.hpp"
#include "util/color.hpp"
#include "util/proximity.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/override_aggregate.hpp"
#include "core/ota_quiet_mode.hpp"
#include "bluetooth.hpp"
#include "crypto.hpp"
#include "components/network/gatt_layout.hpp"
#include "expressions/expression_manager.hpp"
#include "nearby_lamps.hpp"
#include "mesh_link.hpp"
#include "wifi.hpp"
#include "write_router.hpp"

// Defined in lamp.cpp. Each BLE callback does a fixed-size byte copy into a
// pending slot (zero heap on Core 0). Core 1 drains and does all heap work.
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
// CHAR_WISP_OP. drain broadcasts as MSG_CONTROL_OP. Bypasses
// applyRemoteOpLocal so a gossip-relayed wispOp is not re-applied on
// every lamp; only the wisp(s) consume it.
void postPendingWispOpJson(const char* data, size_t len);
void postPendingApplyEffectiveBrightness();
// NVS commits cannot run on Core 0 (NimBLE host task). onDisconnect posts
// this flag; Core 1 force-flushes dispositions so a phone walk-off persists
// the user's slider value.
void postPendingFlushDispositions();

// Read by notifyStateChange() to populate previewActive so the app can
// reflect test state without its own timer.
extern lamp::ExpressionManager expressionManager;

namespace ble_control {

static NimBLEServer*         s_server          = nullptr;
static NimBLEService*        s_service         = nullptr;
static NimBLECharacteristic* s_stateNotify     = nullptr;
static NimBLECharacteristic* s_wifiStateChar   = nullptr;
static NimBLECharacteristic* s_wispStatusChar  = nullptr;
static NimBLECharacteristic* s_wispClaimsChar  = nullptr;
static lamp::Config*         s_config      = nullptr;
static bool                  s_running     = false;

// volatile: written by BLE host task (Core 0), read from Core 1 and scan
// callbacks. Compiler would otherwise cache reads in a register and miss flips.
static volatile bool         s_clientConnected   = false;
// Forces effectiveHomeMode TRUE while set; routes CHAR_BRIGHTNESS to
// home.brightness instead of lamp.brightness.
static volatile bool         s_homeModePageActive = false;
// Prevents the central scan from auto-restarting while a phone holds a
// GATT connection (queried by isScanPaused()).
static volatile bool         s_scanPausedForGattClient = false;

static          uint16_t     s_currentConnHandle = 0xFFFF;

static constexpr uint16_t kTightMinUnits = 12;   //  15.0 ms (units of 1.25 ms)
static constexpr uint16_t kTightMaxUnits = 24;   //  30.0 ms
static constexpr uint16_t kSupervisionTimeoutUnits = 400;  // 4.0 s (units of 10 ms)

bool isClientConnected()   { return s_clientConnected;   }
bool isHomeModePageActive() { return s_homeModePageActive; }
bool isScanPaused()        { return s_scanPausedForGattClient; }

// Per-connection state. NimBLE caps at CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3,
// so a fixed-size array beats std::map overhead for 1-3 entries (linear
// search over 3 slots is cheaper than a red-black-tree lookup on the
// per-write hot path).
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

// Pinned to ATT_MTU 247 minus the 3-byte ATT header rather than reading
// the per-conn negotiated MTU: flutter_blue_plus 2.x doesn't reliably
// surface the negotiated value, so a wire constant lets the app hardcode
// "short = done" without threading MTU state per connection.
static constexpr uint16_t kPageMaxChunkSize = 244;

// static so the compiler can inline this into every BLE callback; it is on
// the per-write hot path.
static ConnSlot* findSlot(uint16_t handle) {
  for (auto& s : s_conn) {
    if (s.handle == handle) return &s;
  }
  return nullptr;
}

// Find the slot for [handle], allocating the first unused slot if none
// matches. Returns nullptr only if all 3 slots are taken (unreachable given
// NimBLE's connection cap). New slots start with authed=false.
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

// Release the slot owned by [handle]. No-op if not tracked.
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

// Reset all slots on stop().
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
  if (s_config->lamp.password.empty()) return true;  // No password — open access
  const ConnSlot* s = findSlot(connHandle);
  return s && s->authed;
}

// Parse a UUID string into 16 bytes in reversed (LE) order to match the
// Dart side's uuidSaltLE16(). The HKDF salt is opaque; both sides just
// need the same bytes.
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

// Decode an inbound write payload. Magic-byte dispatch:
//   0x02 → AES-GCM ciphertext; successful decrypt implicitly authenticates
//          (GCM tag verifies the lamp password).
//   0x01 → explicit plaintext prefix, stripped before passing through.
//   other → bare JSON (including legacy webapp '{' first byte), passed as-is.
// Plaintext writes still require a prior CHAR_AUTH success to be authed.
// Returns true on success; false to silently reject.
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
    if (!slot) return false;  // all slots taken — shouldn't happen w/ NimBLE cap
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
    // Allocate up front so a plaintext CHAR_AUTH write has a slot to land
    // in. If all 3 slots are taken, the connection stays unauthed.
    findOrAllocSlot(handle);

    server->setDataLen(handle, 251);
    NimBLEDevice::setMTU(TARGET_MTU);

    // Request a tight interval for live-preview throughput. Android may
    // decline, but when accepted this raises write throughput from ~20/sec
    // at the default ~49ms interval to ~33-66/sec, eliminating slider lag.
    server->updateConnParams(handle, kTightMinUnits, kTightMaxUnits, 0,
                             kSupervisionTimeoutUnits);
    s_currentConnHandle = handle;

    s_scanPausedForGattClient = true;
    NimBLEDevice::getScan()->stop();

    // wifi::tick skips background scans while a client is connected so the
    // radio stays focused on BT.
    s_clientConnected = true;
    s_homeModePageActive = false;
    postPendingApplyEffectiveBrightness();

#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] Client connected, handle=%u (BT session active)\n", handle);
#endif
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    uint16_t handle = connInfo.getConnHandle();
    freeSlot(handle);

    s_scanPausedForGattClient = false;
    NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);

    s_clientConnected = false;
    s_homeModePageActive = false;
    // Clear editing flags so a crashed/backgrounded app can't keep the
    // wisp's overrides locked out; reconnected sessions re-open via
    // CHAR_EDIT_SESSION.
    lamp::overrides.base.setOperatorEditing(false);
    lamp::overrides.shade.setOperatorEditing(false);
    lamp::overrides.brightness.setOperatorEditing(false);
    // Re-assert wisp paint in case a BLE write stomped the target gradient
    // before disconnecting.
    lamp::overrides.base.reassertHold();
    lamp::overrides.shade.reassertHold();
    s_currentConnHandle = 0xFFFF;
    postPendingApplyEffectiveBrightness();
    // Force-flush dispositions so the user's final slider value persists
    // if the lamp loses power before the debounce window elapses.
    postPendingFlushDispositions();
    // Skip the commit idle window so a quick edit-then-disconnect doesn't
    // lose the user's last change.
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
      // Ciphertext path already marked the slot authed — nothing more to do.
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

class BrightnessCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    std::string val = c->getValue();
    if (val.empty()) return;
    uint8_t level = static_cast<uint8_t>(val[0]);
    if (level > 100) level = 100;
#ifdef LAMP_DEBUG
    Serial.printf("[ble] brightness recv level=%u t_us=%lu\n",
                  level, (unsigned long)micros());
#endif
    postPendingBrightness(static_cast<int8_t>(level & 0x7F));
  }
};

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

// No separate per-flag recency window: a recency cutoff shorter than the
// prune cutoff produces "ghost rows" (entry visible, both badges gone)
// between the two deadlines.
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
    // Take the max of both transport timestamps as the canonical lastSeen.
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
    // Omit zero/idle values to keep the JSON compact.
    if (p.firmwareVersion != 0) o["fwVersion"] = p.firmwareVersion;
    if (p.otaState != 0) o["otaState"] = p.otaState;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

// Auth-gated on read and write: the disposition map reveals peer
// relationships that an unauthenticated scanner shouldn't see.
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
    // NVS write runs on Core 1 so it serialises against the settings_blob
    // drain (shared `prefs` instance can't tolerate concurrent begin/end).
    postPendingSocialDispositionsJson(val.data(), val.size());
  }
};

// Both the onRead callback and notifyWispStatus() call
// getWispStatusReadJson() so they always hand back the same bytes.
// Auth-gated: a wisp's runtime state shouldn't be readable to an
// unauthenticated scanner.
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

class SettingsBlobCallback : public NimBLECharacteristicCallbacks {
  // Write-and-reboot save target; reads go through the page protocol.
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (lamp::ota_quiet_mode::isQuiet()) return;
    static const auto uuid = uuidSaltLE(CHAR_SETTINGS_BLOB);
    const uint16_t handle = connInfo.getConnHandle();
    const std::string raw = c->getValue();
    // Settings blob can exceed the standard op bound; +64 for ciphertext
    // overhead. NimBLE enforces MTU on the link; this is a sanity ceiling.
    if (raw.size() > 4096 + 64) return;
    std::string json;
    bool authed = false;
    if (!decodeIncomingOp(raw, handle, uuid.data(), "settingsBlob", json, authed)) {
#ifdef LAMP_DEBUG
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

    // Runs after the expressionOp drain so just-arrived expression edits
    // are in config.expressions before settings_blob persists.
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

// Page protocol: CHAR_PAGE_CTRL onWrite snapshots a named section into the
// connection slot and resets a cursor; CHAR_PAGE_DATA onRead returns the next
// chunk. The app reads until a short chunk arrives.
//
// This sidesteps the vendored ble_att.h 512-byte ceiling (no #ifndef guard,
// so it overrides -D BLE_ATT_ATTR_MAX_LEN=1024): the expressions section
// on a 3-expression lamp serialises to ~579 bytes and came up empty via a
// single characteristic setValue().
//
// NimBLE fires onRead once per GATT_READ_REQ; seeding the AttValue with
// <=kPageMaxChunkSize bytes forces the app to issue a new read per chunk
// rather than using ATT_READ_BLOB_REQ against a cached value.

using SectionSerializer = void(*)(std::string&);

struct SectionEntry {
  const char*       name;
  SectionSerializer fn;
};

// Lambdas capture only globals so they decay to plain function pointers
// (no std::function footprint). tick() proactively rebuilds dirty caches on
// Core 1; the portMUX inside *SectionJsonCached() covers the race if a
// CTRL-write arrives before tick fires.
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
        // assign() reuses the string's existing capacity (heap growth, not
        // churn). Capture MTU at snapshot time so the chunk size is stable
        // even if conn-params shift mid-stream.
        entry.fn(slot->pageSnapshot);
        slot->pageCursor = 0;
        // Floor against the wire constant: peers that negotiate higher than
        // 247 must still satisfy the app's "short = done" heuristic.
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

    // Unknown section: clear so the next DATA read returns empty, which
    // the app treats as "section not found".
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
      // Empty response signals end-of-section to the app ("short chunk = done").
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

// BLE OTA: CHAR_FW_CONTROL carries the OFFER/DONE/ACCEPT/REQ/RESULT control
// plane (write+notify); CHAR_FW_CHUNK carries the high-frequency chunk stream
// (write-without-response). FwChunkCallback calls handleChunkOnRecvTask
// directly on the BLE host task — bounded ~0.5 ms per chunk.

static NimBLECharacteristic*  s_fwControlChar = nullptr;
static lamp::FirmwareReceiver* s_firmwareReceiver = nullptr;

class BleFirmwareTransport : public lamp::FirmwareTransport {
 public:
  void getMyMac(uint8_t out[6]) const override {
    // BT and Wi-Fi share the OUI on ESP32; either address works as a stable
    // sourceMac identifier on the wire.
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
    // Direct call on the BLE host task; bounded ~0.5 ms (one OTA write +
    // bitmap set). A missed chunk triggers the receiver's REQ watchdog.
    s_firmwareReceiver->handleChunkOnRecvTask(p);
  }
};

void tick() {
  if (!s_running || !s_config) return;

  // Proactively rebuild dirty section caches on Core 1 so the BLE host
  // task (Core 0) never serialises JSON inside a NimBLE callback.
  // Cached() accessors are no-ops when the cache is clean.
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

  // createServer() returns the existing instance if one was already created
  // (NimBLE supports only one server per device).
  s_server = NimBLEDevice::createServer();
  // Pass deleteCallbacks=false: the callbacks object lives for the process
  // lifetime and must not be freed by NimBLE.
  s_server->setCallbacks(new ControlServerCallbacks(), false);

  // NimBLE 2.x default is FALSE: advertising does not auto-restart on
  // disconnect. Without this the lamp goes undiscoverable until reboot.
  s_server->advertiseOnDisconnect(true);

  s_service = s_server->createService(SERVICE_UUID);

  // write-with-response so the app receives a GATT ack. AES-GCM (0x02
  // prefix) auto-authenticates via the GCM tag; plaintext writes compare
  // against lamp.password. No link-layer bonding required.
  s_service->createCharacteristic(CHAR_AUTH,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new AuthCallback());

  // WRITE_NR lets the app skip the per-write GATT ACK round-trip. WRITE
  // alone capped throughput at ~5 Hz at the default ~49ms interval
  // (visibly laggy slider drag). WRITE is retained so older builds pass
  // the GATT property check.
  static constexpr uint32_t LIVE_WRITE_PROPS =
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;

  s_service->createCharacteristic(CHAR_BRIGHTNESS, LIVE_WRITE_PROPS)
      ->setCallbacks(new BrightnessCallback());
  s_service->createCharacteristic(CHAR_SHADE_COLORS, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonBase, postPendingShadeColorsJson, isAuthed))
              ->setDebugTag("shadeColors"));
  s_service->createCharacteristic(CHAR_BASE_COLORS, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonBase, postPendingBaseColorsJson, isAuthed))
              ->setDebugTag("baseColors"));
  // Parameterless commit signal: payload is ignored, arrival is the signal.
  // allowEmpty(true) so both an empty write and a sentinel byte work.
  s_service->createCharacteristic(CHAR_COMMIT_UUID, LIVE_WRITE_PROPS)
      ->setCallbacks((new WriteRouter(
          /*maxSize=*/4, postPendingCommit, isAuthed))
              ->setDebugTag("commit")
              ->setAllowEmpty(true));
  s_service->createCharacteristic(CHAR_BASE_KNOCKOUT, LIVE_WRITE_PROPS)
      ->setCallbacks(new BaseKnockoutCallback());
  s_service->createCharacteristic(CHAR_HOME_MODE_FOCUS, LIVE_WRITE_PROPS)
      ->setCallbacks(new HomeModeFocusCallback());
  s_service->createCharacteristic(CHAR_EDIT_SESSION, LIVE_WRITE_PROPS)
      ->setCallbacks(new EditSessionCallback());
  // Empty payload is the "test complete" sentinel and must reach the drain;
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
  // Salt is function-local-static so the router holds a stable pointer
  // for the lifetime of the service.
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

  s_service->createCharacteristic(CHAR_PAGE_CTRL, NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new PageCtrlCallback());
  s_service->createCharacteristic(CHAR_PAGE_DATA, NIMBLE_PROPERTY::READ)
      ->setCallbacks(new PageDataCallback());

  // Social dispositions: read + write, both auth-gated.
  s_service->createCharacteristic(
      CHAR_SOCIAL_DISPOSITIONS,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new SocialDispositionsCallback());

  static const auto kRemoteOpSalt = uuidSaltLE(CHAR_REMOTE_OP);
  s_service->createCharacteristic(CHAR_REMOTE_OP,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingRemoteOpJson, isAuthed,
          decodeIncomingOp, kRemoteOpSalt.data(), "remoteOp"))
              ->setDebugTag("remoteOp"));

  // wispOp is plaintext: the wisp owns the vocabulary and it is open-set,
  // so encrypting it would force a lamp firmware bump for every new wisp op.
  s_service->createCharacteristic(CHAR_WISP_OP, NIMBLE_PROPERTY::WRITE)
      ->setCallbacks((new WriteRouter(
          lamp::kPendingJsonOp, postPendingWispOpJson, isAuthed))
              ->setDebugTag("wispOp"));

  s_wispStatusChar = s_service->createCharacteristic(
      CHAR_WISP_STATUS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_wispStatusChar->setCallbacks(new WispStatusCallback());
  s_wispStatusChar->setValue(lamp::nearbyLamps.getWispStatusReadJson());

  // Write-only: reads go through the page protocol. Full config save is
  // AES-GCM encrypted; no link-layer bonding required.
  s_service->createCharacteristic(CHAR_SETTINGS_BLOB,
      NIMBLE_PROPERTY::WRITE)
      ->setCallbacks(new SettingsBlobCallback());

  s_fwControlChar = s_service->createCharacteristic(
      CHAR_FW_CONTROL,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_fwControlChar->setCallbacks(new FwControlCallback());
  s_service->createCharacteristic(CHAR_FW_CHUNK, LIVE_WRITE_PROPS)
      ->setCallbacks(new FwChunkCallback());

  s_stateNotify = s_service->createCharacteristic(CHAR_STATE_NOTIFY,
                                                  NIMBLE_PROPERTY::NOTIFY);
  s_stateNotify->setValue("{}");

  // Schema version — read-only. The app reads this to detect the lamp's
  // attribute-layout version; lamps predating it read as absent and the app
  // falls back to legacy behavior. See gatt_layout.hpp.
  static const uint8_t kSchemaVersionValue = kGattSchemaVersion;
  s_service->createCharacteristic(CHAR_SCHEMA_VERSION, NIMBLE_PROPERTY::READ)
      ->setValue(&kSchemaVersionValue, 1);

  // Binary blob: [count:1][lampMac:6]*count. count=0 when no claim heard
  // in 60 s. Separate from CHAR_WISP_STATUS because that payload is already
  // at the MTU soft budget (466/509 B).
  class WispClaimsCallback : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
      if (!isAuthed(connInfo.getConnHandle())) {
        static const uint8_t kEmpty[1] = {0};
        c->setValue(kEmpty, 1);
        return;
      }
      static uint8_t buf[1 + lamp_protocol::kMaxWispClaimEntries * 6];
      size_t n = lamp::nearbyLamps.buildWispClaimsBlob(buf, sizeof(buf),
                                                        millis());
      c->setValue(buf, n);
    }
  };
  s_wispClaimsChar = s_service->createCharacteristic(CHAR_WISP_CLAIMS,
                                                      NIMBLE_PROPERTY::READ);
  s_wispClaimsChar->setCallbacks(new WispClaimsCallback());
  {
    static const uint8_t kInitial[1] = {0};
    s_wispClaimsChar->setValue(kInitial, 1);
  }

  s_service->start();

  // Mismatch means a characteristic was added/removed/reordered without
  // updating gatt_layout.hpp, which silently stales paired app installs.
  // Non-fatal: do not brick a deployed lamp over a dev-time invariant.
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

  // Do not touch advertising: BluetoothComponent::begin() already configured
  // it with the color-sync manufacturer data. The GATT service is discovered
  // after connection, not in the adv packet (a 128-bit UUID overflows the
  // 31-byte limit).

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
    s_wispClaimsChar = nullptr;
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
  // first-boot config UI — it tears itself down after the 120 s boot
  // window regardless, so this just brings the inevitable forward.
#if LAMP_WEBAPP_ENABLED
  webapp::shutdownForOta();
#endif
#ifdef LAMP_DEBUG
  Serial.println("[ble_control] paused adv + scan + softAP for OTA");
#endif
}

// Kills the active connection so the phone can't fight the OTA chunk stream
// for radio time. The GATT server stays up; the phone reconnects after
// RESULT/abort or the OTA reboot. Safe to call from any task:
// NimBLEServer::disconnect serialises via the NimBLE host task's event queue.
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
  // Two independent scan-pause flags: OTA and active GATT client. Only
  // restart if the client flag isn't also holding the scan down.
  if (!s_scanPausedForGattClient) {
    NimBLEDevice::getScan()->start(BLE_GAP_SCAN_TIME_MS);
  }
  NimBLEDevice::getAdvertising()->start();
#ifdef LAMP_DEBUG
  Serial.println("[ble_control] resumed adv + scan after OTA");
#endif
}

// Bind the BLE transport so CHAR_FW_CONTROL notifications route back through
// BleFirmwareTransport::sendFrame().
void setFirmwareReceiver(lamp::FirmwareReceiver* receiver) {
  s_firmwareReceiver = receiver;
  if (receiver) {
    receiver->setBleTransport(&s_bleFwTransport);
  }
}

// data/len are ignored; the arrival of the write is the commit signal.
void postPendingCommit(const char* /*data*/, size_t /*len*/) {
  // Single bool is naturally atomic on Xtensa; no portMUX needed.
  lamp::pendingSlots.pendingCommit = true;
}

}  // namespace ble_control
