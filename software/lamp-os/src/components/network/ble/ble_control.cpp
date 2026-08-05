#include "ble_control.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "config/config.hpp"
#include "behaviors/fade_out.hpp"
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "components/webapp/webapp.hpp"
#include "util/color.hpp"
#include "util/json_escape.hpp"
#include "util/heap_probe.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/override_aggregate.hpp"
#include "components/firmware/ota_quiet_mode.hpp"
#include "ble_gap.hpp"
#include "crypto.hpp"
#include "components/network/ble/gatt_layout.hpp"
#include "components/network/ble/social_scan.hpp"
#include "expressions/expression_manager.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/mesh/proximity.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "components/network/transport/wifi.hpp"
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
void resetZonePreviewOnDisconnect();

// Read by notifyStateChange() to populate previewActive so the app can
// reflect test state without its own timer.
extern lamp::ExpressionManager expressionManager;

namespace ble_control {

static_assert(toNimbleProps(GP_READ) == NIMBLE_PROPERTY::READ &&
                  toNimbleProps(GP_WRITE) == NIMBLE_PROPERTY::WRITE &&
                  toNimbleProps(GP_WRITE_NR) == NIMBLE_PROPERTY::WRITE_NR &&
                  toNimbleProps(GP_NOTIFY) == NIMBLE_PROPERTY::NOTIFY,
              "toNimbleProps out of sync with NIMBLE_PROPERTY bits");

static NimBLEServer*         s_server          = nullptr;
static NimBLEService*        s_service         = nullptr;
static NimBLECharacteristic* s_stateNotify     = nullptr;
static NimBLECharacteristic* s_wifiStateChar   = nullptr;
static NimBLECharacteristic* s_wispStatusChar  = nullptr;
static NimBLECharacteristic* s_wispClaimsChar  = nullptr;
static lamp::Config*         s_config      = nullptr;
static bool                  s_running     = false;
static bool                  s_bleDown     = false;

// volatile: written by BLE host task (Core 0), read from Core 1 and scan
// callbacks. Compiler would otherwise cache reads in a register and miss flips.
static volatile bool         s_clientConnected   = false;
// Forces effectiveHomeMode TRUE while set; routes CHAR_BRIGHTNESS to
// home.brightness instead of lamp.brightness.
static volatile bool         s_homeModePageActive = false;
// Prevents the central scan from auto-restarting while a phone holds a
// GATT connection (queried by isScanPaused()).
static volatile bool         s_scanPausedForGattClient = false;
// Held true for an OTA window (pauseRadioForOta): quiets adv + scan.
static volatile bool         s_otaRadioPaused = false;
// Set by the app (CHAR_EDIT_SESSION social-view selector) while the
// Social/Network page is open. Gates the duty-cycled passive scan below.
// Cleared on BLE disconnect.
static volatile bool         s_socialViewActive = false;
// millis() of the last throughput-sensitive BLE write (color/brightness/
// knockout/OTA chunk). The social scan skips a window while a write fired
// within SocialScanTuning::idleGuardMs so it never competes during a live
// drag or OTA. Written on Core 0, read on Core 1.
static volatile uint32_t     s_lastAppWriteMs = 0;

// Core 1 only (tick): duty-cycled passive-scan state for the Social view.
static SocialScanGate        s_socialScan;
static const SocialScanTuning s_socialScanTuning;
static bool                  s_socialScanPrevActive = false;

static          uint16_t     s_currentConnHandle = 0xFFFF;

static std::function<lamp::GreetingState()> s_greetingStateProvider;

static constexpr uint16_t kTightMinUnits = 12;   //  15.0 ms (units of 1.25 ms)
static constexpr uint16_t kTightMaxUnits = 24;   //  30.0 ms
static constexpr uint16_t kSupervisionTimeoutUnits = 400;  // 4.0 s (units of 10 ms)

bool isClientConnected()   { return s_clientConnected;   }
bool isHomeModePageActive() { return s_homeModePageActive; }
bool isScanPaused()        { return s_scanPausedForGattClient; }

void noteAppWriteActivity() { s_lastAppWriteMs = millis(); }

// Per-connection state. NimBLE caps at CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
// (advertising stops on connect, so only one app connects at a time), so a
// fixed-size array beats std::map overhead.
struct ConnSlot {
  uint16_t                    handle;
  bool                        authed;
  lamp::crypto::PerConnState  crypto;
  std::string                 pageSnapshot;
  uint16_t                    pageCursor;
  uint16_t                    pageMtu;
  // Points at the buffer being paged out. For build-based sections it is
  // &pageSnapshot; for reference-based sections (exprcat) it is the immutable
  // static, so no ~9.6 KB per-connection copy is pinned for the session.
  const std::string*          pageSource;
};
static constexpr uint16_t kUnusedHandle = 0xFFFF;
static constexpr size_t   kMaxConns     = 1;
static std::array<ConnSlot, kMaxConns> s_conn{{
  {kUnusedHandle, false, {}, {}, 0, 0, nullptr},
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
// matches. Returns nullptr only if the slot is taken (unreachable given
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
    freeSlot->pageSource = nullptr;
  }
  return freeSlot;
}

// Release the slot owned by [handle]. No-op if not tracked.
static void freeSlot(uint16_t handle) {
  if (auto* s = findSlot(handle)) {
    s->handle = kUnusedHandle;
    s->authed = false;
    s->crypto = lamp::crypto::PerConnState{};
    std::string().swap(s->pageSnapshot);  // reclaim, not just .clear()
    s->pageCursor = 0;
    s->pageMtu    = 0;
    s->pageSource = nullptr;
  }
}

// Reset all slots on stop().
static void clearAllSlots() {
  for (auto& s : s_conn) {
    s.handle = kUnusedHandle;
    s.authed = false;
    s.crypto = lamp::crypto::PerConnState{};
    std::string().swap(s.pageSnapshot);
    s.pageCursor = 0;
    s.pageMtu    = 0;
    s.pageSource = nullptr;
  }
}

static constexpr uint16_t TARGET_MTU = 512;

static bool isAuthed(uint16_t connHandle) {
  if (s_config->lamp.password.empty()) return true;  // No password, open access
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
    if (!slot) return false;  // all slots taken; NimBLE cap bounds this
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
    // in. If the slot is taken, the connection stays unauthed.
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
    lamp::logHeap("ble-connect");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    uint16_t handle = connInfo.getConnHandle();
    freeSlot(handle);

    s_scanPausedForGattClient = false;
    // Resume the continuous scan the connect paused.
    NimBLEDevice::getScan()->start(0);

    s_clientConnected = false;
    s_homeModePageActive = false;
    s_socialViewActive = false;
    // Clear editing flags so a crashed/backgrounded app can't keep the
    // wisp's overrides locked out; reconnected sessions re-open via
    // CHAR_EDIT_SESSION.
    lamp::overrides.base.setOperatorEditing(false);
    lamp::overrides.shade.setOperatorEditing(false);
    lamp::overrides.brightness.setOperatorEditing(false);
    resetZonePreviewOnDisconnect();
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
    lamp::logHeap("ble-disconnect");
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

class BrightnessCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) return;
    if (lamp::ota_quiet_mode::isQuiet()) return;
    noteAppWriteActivity();
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
    // 0x08 is not an edit surface: the app on the Social/Network page toggles
    // the duty-cycled passive scan that sights BLE-only peers while connected.
    if (surface & 0x08) s_socialViewActive = open;
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
    noteAppWriteActivity();
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
//
// Direct string emit, no JsonDocument: at roster capacity the payload is
// ~15 KB and an ArduinoJson build peaks at ~3x that. Colors emit as 8
// uppercase hex chars "RRGGBBWW" (the app also parses the legacy 4-int
// array). Zero/idle values are omitted to keep the JSON compact.
static inline void fnv1a(uint32_t& h, const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (len--) { h ^= *p++; h *= 16777619u; }
}

// A BLE section OOM degrades to a graceful-empty serve; keep it visible on
// every channel, throttled to one line per 5000ms.
static void logBleSectionOom(const char* what) {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (now - lastMs < 5000 && lastMs != 0) return;
  lastMs = now;
  Serial.printf("[ble_control] %s OOM; serving graceful-empty\n", what);
}

// The ~9.6 KB expression catalog is the single largest resident heap block, so
// it isn't pinned for the process lifetime. Built once per connection on Core 1
// (tick, off the NimBLE host task) and freed on disconnect so the block is
// reclaimable for roster growth while no app is connected. Guarded like the
// nearby cache: Core 1 swaps a validated build in under the mutex, and the
// exprcat section copies it out under the same mutex on section-open, so the
// Core 0 host task never reads a string being reallocated mid-flight.
static SemaphoreHandle_t exprCatalogMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
static std::string s_exprCatalog;             // guarded by exprCatalogMutex()
static bool        s_exprCatalogReady = false;  // guarded by exprCatalogMutex()

// Core 1 (tick). Build once per connection, free on disconnect. Publishes only
// a validated non-empty catalog; a failed/partial serialize leaves it not-ready
// so the next tick retries. The app reads exprcat last in its section load
// (after five other multi-chunk reads), so this has the whole settings-load
// window to finish before the section is opened.
static void maintainExprCatalog() {
  static bool builtThisConn = false;
  if (s_clientConnected) {
    if (builtThisConn) return;
    std::string local;
    try {
      local = ::expressionManager.registry().serializeCatalog();
    } catch (const std::bad_alloc&) {
      logBleSectionOom("exprcat");
      return;
    }
    // ArduinoJson's allocator does not throw; a partial-heap serialize yields a
    // truncated or "null" document. Require a plausible object before
    // publishing so a short build is retried, never served as the catalog.
    if (local.size() < 2 || local.front() != '{') {
      logBleSectionOom("exprcat");
      return;
    }
    xSemaphoreTake(exprCatalogMutex(), portMAX_DELAY);
    s_exprCatalog.swap(local);
    s_exprCatalogReady = true;
    xSemaphoreGive(exprCatalogMutex());
    builtThisConn = true;
  } else if (builtThisConn) {
    builtThisConn = false;
    xSemaphoreTake(exprCatalogMutex(), portMAX_DELAY);
    std::string().swap(s_exprCatalog);
    s_exprCatalogReady = false;
    xSemaphoreGive(exprCatalogMutex());
  }
}

static void logNearbyHeapSkip(uint32_t largest, uint32_t needed, uint32_t lamps) {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (now - lastMs < 5000 && lastMs != 0) return;
  lastMs = now;
  Serial.printf("[ble_control] nearby build skipped, low heap: largest=%u needed=%u lamps=%u\n",
                largest, needed, lamps);
}

static constexpr uint32_t kNearbyHeapMargin = 1024;

// Builds into `out`, reused in place (cleared, capacity retained) so a
// steady-state rebuild never allocates once the buffer has grown to the
// fleet's high-water size.
static void buildNearbyJson(uint32_t& membershipHashOut,
                            std::vector<lamp::RosterEntry>& lamps,
                            std::string& out) {
  // Sort by name for stable rendering.
  std::sort(lamps.begin(), lamps.end(),
            [](const lamp::RosterEntry& a, const lamp::RosterEntry& b) {
              return std::strcmp(a.name, b.name) < 0;
            });
  out.clear();
  // Upper bound: ~240 B of fixed fields per entry (fwChannel added) plus
  // worst-case 6 B per escaped name char, so the string never reallocs
  // mid-build.
  size_t cap = 2;
  for (const auto& p : lamps) cap += 240 + 6 * std::strlen(p.name);
  out.reserve(cap);
  out += '[';
  char buf[96];
  bool first = true;
  const uint32_t now = millis();
  uint32_t hash = 2166136261u;
  for (const auto& p : lamps) {
    if (!first) out += ',';
    first = false;
    // Membership hash over the app-visible fields only. lastSeenMs and rssi
    // freshen on every BLE adv (~30 Hz); folding them would churn the rev.
    const uint8_t flags =
        (p.lastSeenNearMs != 0 ? 1 : 0) | (p.lastSeenMeshMs != 0 ? 2 : 0) |
        (lamp::isNearNow(p.lastSeenNearMs, now, LAMP_PRUNE_TIME_MS) ? 4 : 0) |
        (p.hasMac ? 8 : 0) | (p.hasOtaSendingTo ? 16 : 0);
    fnv1a(hash, p.name, std::strlen(p.name));
    fnv1a(hash, &flags, 1);
    if (p.hasMac) fnv1a(hash, p.mac, 6);
    fnv1a(hash, &p.shadeColor, 4);
    fnv1a(hash, &p.baseColor, 4);
    fnv1a(hash, &p.firmwareVersion, sizeof(p.firmwareVersion));
    fnv1a(hash, p.fwChannel, std::strlen(p.fwChannel));
    fnv1a(hash, &p.otaState, 1);
    if (p.hasOtaSendingTo) fnv1a(hash, p.otaSendingTo, 6);
    out += "{\"name\":\"";
    lamp::appendJsonEscaped(out, p.name);
    // Max of both transport timestamps is the canonical lastSeen.
    const uint32_t lastSeenMs = std::max(p.lastSeenNearMs, p.lastSeenMeshMs);
    snprintf(buf, sizeof(buf), "\",\"lastSeenMs\":%lu,\"viaBle\":%s,\"viaEspNow\":%s,\"near\":%s",
             static_cast<unsigned long>(lastSeenMs),
             p.lastSeenNearMs != 0 ? "true" : "false",
             p.lastSeenMeshMs != 0 ? "true" : "false",
             lamp::isNearNow(p.lastSeenNearMs, now, LAMP_PRUNE_TIME_MS) ? "true" : "false");
    out += buf;
    if (p.hasMac) {
      char macBuf[18];
      lamp::formatBdAddr(p.mac, macBuf);
      out += ",\"lampId\":\"";
      out += macBuf;
      out += '"';
    }
    if (p.lastRssi != -127) {
      snprintf(buf, sizeof(buf), ",\"rssi\":%d", p.lastRssi);
      out += buf;
    }
    snprintf(buf, sizeof(buf),
             ",\"shade\":\"%02x%02x%02x%02x\",\"base\":\"%02x%02x%02x%02x\"",
             p.shadeColor.r, p.shadeColor.g, p.shadeColor.b, p.shadeColor.w,
             p.baseColor.r, p.baseColor.g, p.baseColor.b, p.baseColor.w);
    out += buf;
    if (p.firmwareVersion != 0) {
      snprintf(buf, sizeof(buf), ",\"fwVersion\":%lu",
               static_cast<unsigned long>(p.firmwareVersion));
      out += buf;
    }
    snprintf(buf, sizeof(buf), ",\"fwChannel\":\"%s\"", p.fwChannel);
    out += buf;
    if (p.otaState != 0) {
      snprintf(buf, sizeof(buf), ",\"otaState\":%u", p.otaState);
      out += buf;
    }
    if (p.hasOtaSendingTo) {
      char macBuf[18];
      lamp::formatBdAddr(p.otaSendingTo, macBuf);
      out += ",\"otaSendingTo\":\"";
      out += macBuf;
      out += '"';
    }
    out += '}';
  }
  out += ']';
  membershipHashOut = hash;
}

// nearby is served from a Core 1-built cache like the Config section
// caches: the NimBLE host task must never run the bulk build (roster
// copy + serialize) on Core 0.
static SemaphoreHandle_t nearbyCacheMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}
static std::string s_nearbyJsonCache = "[]";

// Bumped by refreshNearbyJsonCache when the app-visible roster fields change
// (excluding the ~30 Hz rssi/lastSeen churn). Surfaced in every stateNotify
// payload so the app re-reads nearby on a push instead of polling.
static uint32_t s_nearbyRev = 0;

void copyNearbyJson(std::string& out) {
  xSemaphoreTake(nearbyCacheMutex(), portMAX_DELAY);
  out = s_nearbyJsonCache;
  xSemaphoreGive(nearbyCacheMutex());
}

// Core 1 only (tick). Rebuilds only when the roster changed, at most
// once per second; BLE advs freshen lastSeen at ~30 Hz per peer, so an
// unthrottled rebuild would run every loop.
static void refreshNearbyJsonCache() {
  static uint32_t builtGen    = 0;
  static uint32_t lastBuildMs = 0;
  static uint32_t lastHash    = 0;
  static bool     seeded      = false;
  const uint32_t gen = lamp::lampRoster.generation();
  if (gen == builtGen) return;
  const uint32_t now = millis();
  if (now - lastBuildMs < 1000) return;

  auto& lamps = lamp::lampRoster.getAll();
  uint32_t needed = 2;
  for (auto& e : lamps) needed += 240 + 6 * std::strlen(e.name);
  const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largest < needed + kNearbyHeapMargin) {
    lastBuildMs = now;
    logNearbyHeapSkip(largest, needed, lamps.size());
    return;
  }

  uint32_t hash = 0;
  // Persistent scratch: buildNearbyJson refills it in place, then it swaps
  // buffers with the cache. Both are long-lived, so after warmup the two
  // heap buffers just ping-pong with no per-build alloc/free.
  static std::string s_nearbyBuildScratch;
  try {
    buildNearbyJson(hash, lamps, s_nearbyBuildScratch);
  } catch (const std::bad_alloc&) {
    logBleSectionOom("nearby");
    lastBuildMs = now;
    return;
  }
  xSemaphoreTake(nearbyCacheMutex(), portMAX_DELAY);
  s_nearbyJsonCache.swap(s_nearbyBuildScratch);
  xSemaphoreGive(nearbyCacheMutex());
  builtGen    = gen;
  lastBuildMs = now;
  if (!seeded) {
    seeded   = true;
    lastHash = hash;
    return;
  }
  if (hash != lastHash) {
    lastHash = hash;
    ++s_nearbyRev;
    notifyStateChange();
  }
}

// Auth-gated on read and write: the disposition map reveals peer
// relationships that an unauthenticated scanner shouldn't see.
class SocialDispositionsCallback : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (!isAuthed(connInfo.getConnHandle())) {
      c->setValue("");
      return;
    }
    try {
      c->setValue(s_config->asDispositionsJson().c_str());
    } catch (const std::bad_alloc&) {
      c->setValue("");
      logBleSectionOom("dispositions");
      return;
    }
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
    c->setValue(lamp::lampRoster.getWispStatusReadJson());
  }
};

void notifyWispStatus() {
  if (!s_wispStatusChar) return;
  auto json = lamp::lampRoster.getWispStatusReadJson();
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
  const std::string err = wifi::lastError();
  if (!err.empty()) {
    doc["lastError"] = err;
  }
  if (includeScanResults) {
    auto results = wifi::peekScanResults();
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
    try {
      c->setValue(buildWifiStateJson(true));
    } catch (const std::bad_alloc&) {
      c->setValue("");
      logBleSectionOom("wifi");
    }
  }
};

void notifyWifiState() {
  if (!s_wifiStateChar) return;
  try {
    s_wifiStateChar->setValue(buildWifiStateJson(true));
  } catch (const std::bad_alloc&) {
    logBleSectionOom("wifi");
    return;
  }
  s_wifiStateChar->notify();
}

class SettingsBlobCallback : public NimBLECharacteristicCallbacks {
  // Write-and-reboot save target; reads go through the page protocol.
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    if (lamp::ota_quiet_mode::isQuiet()) return;
    static const auto uuid = uuidSaltLE(CHAR_SETTINGS_BLOB);
    const uint16_t handle = connInfo.getConnHandle();
    const std::string raw = c->getValue();
    // Anything larger than this cannot decode into a payload that fits the
    // pending slot. NimBLE enforces MTU on the link; this is a sanity ceiling.
    if (raw.size() > lamp::kPendingSettingsBlob + lamp::crypto::WIRE_OVERHEAD) return;
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

    // The bare-plaintext decode path passes raw through unshrunk, so the
    // decoded JSON can still exceed the slot by up to WIRE_OVERHEAD bytes.
    if (json.size() > lamp::kPendingSettingsBlob) {
#ifdef LAMP_DEBUG
      Serial.printf("[ble_control] settings_blob too large for pending slot: %u > %u\n",
                    (unsigned)json.size(), (unsigned)lamp::kPendingSettingsBlob);
#endif
      return;
    }
    // Runs after the expressionOp drain so just-arrived expression edits
    // are in config.expressions before settings_blob persists.
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

// fn serialises the section into the connection's pageSnapshot on open.
struct SectionEntry {
  const char*                 name;
  SectionSerializer           fn;
};

// Lambdas capture only globals so they decay to plain function pointers
// (no std::function footprint). Cached() accessors rebuild and copy out
// under Config's cache mutex, so calling here on the Core 0 host task is
// safe against a concurrent Core 1 rebuild; tick() pre-warms the caches so
// the rebuild rarely runs here. nearby and exprcat copy their Core 1-built
// caches out under a mutex; exprcat serves empty until maintainExprCatalog()
// has published a validated build, so a not-yet-ready catalog reads as "no
// catalog" (older-firmware path) rather than a truncated one.
// wispclaims is binary, not JSON: the full accumulated fleet blob exceeds
// the 512-byte ATT ceiling that caps the CHAR_WISP_CLAIMS direct read.
// wisppalette is binary too (raw RGBW): a dedicated read path so the
// frequent palette-less status NOTIFY can't clobber it.
static const std::array<SectionEntry, 9> kSections = {{
  {"lamp",    [](std::string& out) { s_config->lampSectionJsonCached(out); }},
  {"base",    [](std::string& out) { s_config->baseSectionJsonCached(out); }},
  {"shade",   [](std::string& out) { s_config->shadeSectionJsonCached(out); }},
  {"expr",    [](std::string& out) { s_config->expressionsSectionJsonCached(out); }},
  {"home",    [](std::string& out) { s_config->homeSectionJsonCached(out); }},
  {"nearby",  [](std::string& out) { copyNearbyJson(out); }},
  {"exprcat", [](std::string& out) {
    xSemaphoreTake(exprCatalogMutex(), portMAX_DELAY);
    if (s_exprCatalogReady) out = s_exprCatalog; else out.clear();
    xSemaphoreGive(exprCatalogMutex());
  }},
  {"wispclaims", [](std::string& out) {
    static uint8_t buf[1 + lamp::WispFleetCache::kCapacity * 12];
    const size_t n = lamp::lampRoster.buildWispClaimsBlob(buf, sizeof(buf),
                                                          millis());
    out.assign(reinterpret_cast<const char*>(buf), n);
  }},
  {"wisppalette", [](std::string& out) {
    static uint8_t buf[200];
    const size_t n = lamp::lampRoster.copyManualPaletteBlob(buf, sizeof(buf));
    out.assign(reinterpret_cast<const char*>(buf), n);
  }},
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
        // Serialises into pageSnapshot, reusing its existing capacity.
        try {
          entry.fn(slot->pageSnapshot);
        } catch (const std::bad_alloc&) {
          slot->pageSnapshot.clear();
          slot->pageCursor = 0;
          slot->pageMtu    = 0;
          slot->pageSource = nullptr;
          logBleSectionOom(entry.name);
          return;
        }
        slot->pageSource = &slot->pageSnapshot;
        slot->pageCursor = 0;
        // Cap the chunk at the baseline-MTU page size even when this link
        // negotiated higher; the app reads until an empty terminator, so
        // chunk size is a transport bound, not an end-of-section signal.
        const uint16_t mtu = connInfo.getMTU();
        const uint16_t cap = mtu > 3 ? mtu - 3 : 0;
        slot->pageMtu = (cap > 0 && cap < kPageMaxChunkSize) ? cap
                                                              : kPageMaxChunkSize;
#ifdef LAMP_DEBUG
        Serial.printf("[ble_control] page CTRL section=%s len=%u mtu=%u chunk=%u\n",
                      entry.name, (unsigned)slot->pageSource->size(),
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
    slot->pageSource = nullptr;
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
    if (!slot || slot->pageMtu == 0 || slot->pageSource == nullptr ||
        slot->pageCursor >= slot->pageSource->size()) {
      // Empty response signals end-of-section: the app reads chunks until empty.
      c->setValue("");
      return;
    }
    const size_t remaining = slot->pageSource->size() - slot->pageCursor;
    const size_t take      = remaining < slot->pageMtu ? remaining
                                                       : slot->pageMtu;
    c->setValue(reinterpret_cast<const uint8_t*>(slot->pageSource->data() +
                                                  slot->pageCursor),
                static_cast<size_t>(take));
    slot->pageCursor += static_cast<uint16_t>(take);
#ifdef LAMP_DEBUG
    Serial.printf("[ble_control] page DATA cursor=%u/%u take=%u\n",
                  (unsigned)slot->pageCursor,
                  (unsigned)slot->pageSource->size(),
                  (unsigned)take);
#endif
  }
};

// BLE OTA: CHAR_FW_CONTROL carries the OFFER/DONE/ACCEPT/REQ/RESULT control
// plane (write+notify); CHAR_FW_CHUNK carries the high-frequency chunk stream
// (write-without-response). FwChunkCallback calls handleChunkOnRecvTask
// directly on the BLE host task; bounded ~0.5 ms per chunk.

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
      slot.offer.hasAuth     = p.hasAuth;
      std::memcpy(slot.offer.digest, p.digest,
                  lamp_protocol::FW_SHA256_FULL_LEN);
      std::memcpy(slot.offer.signature, p.signature, lamp_protocol::FW_SIG_LEN);
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
    noteAppWriteActivity();
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

  // Continuous scan means onScanEnd rarely fires, so prune here at ~1 Hz to
  // age out silent roster peers.
  static uint32_t s_lastPruneMs = 0;
  const uint32_t nowMs = millis();
  if (nowMs - s_lastPruneMs >= 1000) {
    s_lastPruneMs = nowMs;
    lamp::lampRoster.prune(LAMP_PRUNE_TIME_MS);
  }

  // Duty-cycled passive scan for the Social/Network view. The connect-time
  // pause stops the central scan to protect write throughput; while the app
  // holds the Social page open this reopens short windows so BLE-only peers
  // still get sighted into the roster. Windows tear down only while a client
  // holds the link; onDisconnect resumes the continuous scan itself.
  {
    const bool active =
        s_socialViewActive && s_clientConnected && !s_otaRadioPaused;
    if (active && !s_socialScanPrevActive) s_socialScan.arm(nowMs);
    s_socialScanPrevActive = active;
    const SocialScanAction act =
        s_socialScan.step(nowMs, active, s_lastAppWriteMs, s_socialScanTuning);
    if (act == SocialScanAction::OpenWindow) {
      NimBLEScan* scan = NimBLEDevice::getScan();
      scan->setActiveScan(false);
      scan->start(0);
    } else if (act == SocialScanAction::CloseWindow) {
      if (s_scanPausedForGattClient) NimBLEDevice::getScan()->stop();
    }
  }

  // Proactively rebuild dirty section caches on Core 1 so the BLE host
  // task (Core 0) rarely serialises JSON inside a NimBLE callback.
  s_config->lampSectionRebuildIfDirty();
  s_config->baseSectionRebuildIfDirty();
  s_config->shadeSectionRebuildIfDirty();
  s_config->expressionsSectionRebuildIfDirty();
  s_config->homeSectionRebuildIfDirty();
  refreshNearbyJsonCache();

  // Build the expression catalog once per connection (and free it on
  // disconnect) here on Core 1 so the first client read doesn't serialize it
  // inside a Core 0 NimBLE callback and it isn't pinned while no app is on.
  maintainExprCatalog();
}

void setGreetingStateProvider(std::function<lamp::GreetingState()> fn) {
  s_greetingStateProvider = std::move(fn);
}

void notifyStateChange() {
  if (!s_stateNotify) return;
  const bool preview = ::expressionManager.isAnyTestActive();
  std::string payload = "{\"previewActive\":";
  payload += preview ? "true" : "false";
  if (s_greetingStateProvider) {
    const lamp::GreetingState gs = s_greetingStateProvider();
    if (gs.active) {
      payload += ",\"greeting\":{\"active\":true,\"peer\":\"";
      lamp::appendJsonEscaped(payload, gs.peerLampId.c_str());
      payload += "\",\"kind\":\"";
      lamp::appendJsonEscaped(payload, gs.kind.c_str());
      payload += "\"}";
    } else {
      payload += ",\"greeting\":{\"active\":false}";
    }
  }
  char buf[32];
  snprintf(buf, sizeof(buf), ",\"nearbyRev\":%lu}",
           static_cast<unsigned long>(s_nearbyRev));
  payload += buf;
  s_stateNotify->setValue(payload);
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

  // Binary blob: [count:1][mac:6]*count[colorPair:6]*count. count=0 when
  // no claim heard in 60 s. Truncated to the 32 entries the buffer holds
  // (ATT value ceiling is 512 B); the `wispclaims` page section serves the
  // full accumulated fleet. Separate from CHAR_WISP_STATUS because that
  // payload is already at the MTU soft budget (466/509 B).
  class WispClaimsCallback : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
      if (!isAuthed(connInfo.getConnHandle())) {
        static const uint8_t kEmpty[1] = {0};
        c->setValue(kEmpty, 1);
        return;
      }
      static uint8_t buf[1 + lamp_protocol::kMaxWispClaimEntries * 12];
      size_t n = lamp::lampRoster.buildWispClaimsBlob(buf, sizeof(buf),
                                                        millis());
      c->setValue(buf, n);
    }
  };

  // Salts are function-local-static so the routers hold a stable pointer
  // for the lifetime of the service.
  static const auto kWifiOpSalt = uuidSaltLE(CHAR_WIFI_OP);
  static const auto kRemoteOpSalt = uuidSaltLE(CHAR_REMOTE_OP);

  NimBLECharacteristic* schemaVersionChar = nullptr;

  // Callback + static-pointer bindings for kGattLayout, matched by UUID.
  // Creation order and props come from the table alone.
  struct CharBinding {
    const char* uuid;
    NimBLECharacteristicCallbacks* cb;
    NimBLECharacteristic** out;
  };
  const CharBinding bindings[] = {
      // Write-with-response so the app receives a GATT ack. AES-GCM (0x02
      // prefix) auto-authenticates via the GCM tag; plaintext writes compare
      // against lamp.password. No link-layer bonding required.
      {CHAR_AUTH, new AuthCallback(), nullptr},
      {CHAR_BRIGHTNESS, new BrightnessCallback(), nullptr},
      {CHAR_SHADE_COLORS,
       (new WriteRouter(lamp::kPendingJsonBase, postPendingShadeColorsJson,
                        isAuthed))
           ->setDebugTag("shadeColors"),
       nullptr},
      {CHAR_BASE_COLORS,
       (new WriteRouter(lamp::kPendingJsonBase, postPendingBaseColorsJson,
                        isAuthed))
           ->setDebugTag("baseColors"),
       nullptr},
      // Parameterless commit signal: payload is ignored, arrival is the
      // signal. allowEmpty(true) so both an empty write and a sentinel byte
      // work.
      {CHAR_COMMIT_UUID,
       (new WriteRouter(/*maxSize=*/4, postPendingCommit, isAuthed))
           ->setDebugTag("commit")
           ->setAllowEmpty(true),
       nullptr},
      {CHAR_BASE_KNOCKOUT, new BaseKnockoutCallback(), nullptr},
      {CHAR_HOME_MODE_FOCUS, new HomeModeFocusCallback(), nullptr},
      {CHAR_EDIT_SESSION, new EditSessionCallback(), nullptr},
      // Empty payload is the "test complete" sentinel and must reach the
      // drain; allowEmpty(true) on the router.
      {CHAR_EXPRESSION_TEST,
       (new WriteRouter(lamp::kPendingJsonOp, postPendingTestActionJson,
                        isAuthed))
           ->setDebugTag("expressionTest")
           ->setAllowEmpty(true),
       nullptr},
      {CHAR_EXPRESSION_OP,
       (new WriteRouter(lamp::kPendingJsonOp, postPendingExpressionOpJson,
                        isAuthed))
           ->setDebugTag("expressionOp"),
       nullptr},
      {CHAR_WIFI_OP,
       (new WriteRouter(lamp::kPendingJsonOp, postPendingWifiOpJson, isAuthed,
                        decodeIncomingOp, kWifiOpSalt.data(), "wifiOp"))
           ->setDebugTag("wifiOp"),
       nullptr},
      {CHAR_WIFI_STATE, new WifiStateCallback(), &s_wifiStateChar},
      {CHAR_PAGE_CTRL, new PageCtrlCallback(), nullptr},
      {CHAR_PAGE_DATA, new PageDataCallback(), nullptr},
      {CHAR_SOCIAL_DISPOSITIONS, new SocialDispositionsCallback(), nullptr},
      {CHAR_REMOTE_OP,
       (new WriteRouter(lamp::kPendingJsonOp, postPendingRemoteOpJson,
                        isAuthed, decodeIncomingOp, kRemoteOpSalt.data(),
                        "remoteOp"))
           ->setDebugTag("remoteOp"),
       nullptr},
      // wispOp is plaintext: the wisp owns the vocabulary and it is open-set,
      // so encrypting it would force a lamp firmware bump for every new wisp
      // op.
      {CHAR_WISP_OP,
       (new WriteRouter(lamp::kPendingJsonOp, postPendingWispOpJson,
                        isAuthed))
           ->setDebugTag("wispOp"),
       nullptr},
      {CHAR_WISP_STATUS, new WispStatusCallback(), &s_wispStatusChar},
      // Write-only: reads go through the page protocol. Full config save is
      // AES-GCM encrypted; no link-layer bonding required.
      {CHAR_SETTINGS_BLOB, new SettingsBlobCallback(), nullptr},
      {CHAR_FW_CONTROL, new FwControlCallback(), &s_fwControlChar},
      {CHAR_FW_CHUNK, new FwChunkCallback(), nullptr},
      {CHAR_STATE_NOTIFY, nullptr, &s_stateNotify},
      // Read-only. The app reads this to detect the lamp's attribute-layout
      // version; lamps predating it read as absent and the app falls back to
      // legacy behavior. See gatt_layout.hpp.
      {CHAR_SCHEMA_VERSION, nullptr, &schemaVersionChar},
      {CHAR_WISP_CLAIMS, new WispClaimsCallback(), &s_wispClaimsChar},
  };
  constexpr size_t kBindingCount = sizeof(bindings) / sizeof(bindings[0]);

  // Coverage check: a table entry with no binding registers inert, an
  // unused binding registers nothing. Both log loud and continue; do not
  // brick a deployed lamp over a dev-time invariant.
  bool consumed[kBindingCount] = {};
  for (size_t i = 0; i < kGattLayoutCount; ++i) {
    const GattCharDef& def = kGattLayout[i];
    NimBLECharacteristic* c =
        s_service->createCharacteristic(def.uuid, toNimbleProps(def.props));
    const CharBinding* bound = nullptr;
    for (size_t j = 0; j < kBindingCount; ++j) {
      if (!consumed[j] && strcmp(bindings[j].uuid, def.uuid) == 0) {
        consumed[j] = true;
        bound = &bindings[j];
        break;
      }
    }
    if (!bound) {
      Serial.printf("[ble_control] GATT BINDING MISS: no binding for %s\n",
                    def.uuid);
      continue;
    }
    if (bound->cb) c->setCallbacks(bound->cb);
    if (bound->out) *bound->out = c;
  }
  for (size_t j = 0; j < kBindingCount; ++j) {
    if (!consumed[j]) {
      Serial.printf(
          "[ble_control] GATT BINDING UNUSED: %s not in kGattLayout\n",
          bindings[j].uuid);
    }
  }

  if (s_wifiStateChar) s_wifiStateChar->setValue(buildWifiStateJson(false));
  if (s_wispStatusChar) {
    s_wispStatusChar->setValue(lamp::lampRoster.getWispStatusReadJson());
  }
  if (s_stateNotify) s_stateNotify->setValue("{}");
  if (schemaVersionChar) {
    static const uint8_t kSchemaVersionValue = kGattSchemaVersion;
    schemaVersionChar->setValue(&kSchemaVersionValue, 1);
  }
  if (s_wispClaimsChar) {
    static const uint8_t kInitial[1] = {0};
    s_wispClaimsChar->setValue(kInitial, 1);
  }

  s_service->start();

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
  // first-boot config UI; it tears itself down after the 120 s boot
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

bool deinitForWebapp() {
  if (s_bleDown || !NimBLEDevice::isInitialized()) return false;
  lamp::logHeap("ble-pre");
  disconnectGattClientsForOta();
  NimBLEDevice::getScan()->stop();
  stop();
  NimBLEDevice::deinit(true);
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  s_bleDown = true;
  lamp::logHeap("ble-post");
  return true;
}

bool isDownForWebapp() { return s_bleDown; }

void resumeRadioAfterOta() {
  if (!s_otaRadioPaused) return;
  s_otaRadioPaused = false;
  // Two independent scan-pause flags: OTA and active GATT client. Only
  // restart if the client flag isn't also holding the scan down.
  if (!s_scanPausedForGattClient) {
    NimBLEDevice::getScan()->start(0);
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
