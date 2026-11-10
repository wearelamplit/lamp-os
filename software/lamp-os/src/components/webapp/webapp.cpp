#if LAMP_WEBAPP_ENABLED

#include "webapp.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <WiFi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <lampos/clock.hpp>

#include "expr_catalog.h"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/ota_quiet_mode.hpp"
#include "components/network/ble/ble_control.hpp"
#include "behaviors/fade_out.hpp"
#include "components/network/transport/wifi.hpp"
#include "components/webapp/settings_body.hpp"
#include "components/webapp/webapp_deadline.hpp"
#include "config/config_types.hpp"
#include "core/pending_json_slot.hpp"
#include "util/color.hpp"
#include "util/heap_guard.hpp"
#include "util/heap_probe.hpp"

// Same Core 0 → Core 1 drain handoff the BLE writes use; mutating config
// directly from the AsyncTCP task would race the main loop.
void postPendingBaseColorsJson(const char* data, size_t len);
void postPendingShadeColorsJson(const char* data, size_t len);
void postPendingBrightness(int8_t level);
void postPendingEditSession(uint8_t surfaceMask, bool open);
void postPendingTestActionJson(const char* data, size_t len);
void postPendingSocialDispositionsJson(const char* data, size_t len);

extern lamp::FirmwareReceiver firmwareReceiver;

namespace webapp {

static lamp::Config* s_config = nullptr;
static AsyncWebServer* s_server = nullptr;
static AsyncWebSocket* s_ws = nullptr;

// Bumped by the AsyncTCP task's handlers and held by the loop task, so both
// update it by compare-exchange.
static std::atomic<uint32_t> s_deadlineMs{0};
// An armed reboot and when it is due; millis() can read 0, so the flag, not
// the time, says whether one is armed.
static bool s_rebootArmed = false;
static uint32_t s_rebootAtMs = 0;

static void armReboot(uint32_t atMs) {
  s_rebootArmed = true;
  s_rebootAtMs = atMs;
}
// Set only by the save path so rebootNow() fades to black first; the
// browser-left / idle reboots stay a hard restart (no scene to fade).
static bool s_fadeOnReboot = false;
static bool s_apUp = false;
static bool s_serverUp = false;
static bool s_neverExpire = false;

// Set from the WiFi event task (Core 0); consumed by tick() on the loop task.
// Starting/tearing the AsyncWebServer off the event task crash-loops (rst:0x3),
// so the handlers only flip these and defer the work.
static std::atomic<bool> s_pendingServerStart{false};
static std::atomic<bool> s_pendingServerStop{false};

static constexpr uint16_t kHttpPort = 80;

static void bumpDeadline() {
  const uint32_t now = millis();
  const uint32_t idle = now + LAMP_WEBAPP_IDLE_TIMEOUT_MS;
  uint32_t deadline = s_deadlineMs.load();
  while (lampos::isLater(idle, deadline) &&
         !s_deadlineMs.compare_exchange_weak(deadline, idle)) {
  }
}

// 128 covers 5 × (9 hex + 3 quote/comma) + brackets + NUL with headroom.
static void postColors(JsonArrayConst arr, bool base) {
  if (arr.isNull() || arr.size() == 0) return;
  char buf[128];
  const size_t n = serializeJson(arr, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return;
  if (base) postPendingBaseColorsJson(buf, n);
  else      postPendingShadeColorsJson(buf, n);
}

// The full config snapshot served on GET, built once at begin(). Two reasons
// to snapshot rather than call asJsonDocument() per request: (1) it's
// non-const and walks unlocked vectors, so building it on the AsyncTCP task
// would race Core-1 BLE mutations; (2) lamp.password is stripped here, since
// asJsonDocument() emits it in plaintext and it must never reach a soft-AP
// client.
static String s_settingsJson;

static void buildSettingsSnapshot() {
  JsonDocument doc = s_config->asJsonDocument();
  doc["lamp"]["hasPassword"] = !s_config->lamp.password.empty();
  doc["lamp"]["apSecured"] = wifi::softApSecured();
  doc["lamp"]["lampId"] = s_config->lampId();
  doc["lamp"].remove("password");
  s_settingsJson = "";
  serializeJson(doc, s_settingsJson);
}

// A password the AP can't key leaves the network open, so the surface that
// would hand config writes (or the password itself) to any nearby client goes
// read-only. Recover by setting a usable password over BLE.
static bool webSurfaceReadOnly() {
  return !s_config->lamp.password.empty() && !wifi::softApSecured();
}

static void handleGetSettings(AsyncWebServerRequest* req) {
  bumpDeadline();
  req->send(200, "application/json", s_settingsJson);
}

static void handleGetExpressions(AsyncWebServerRequest* req) {
  // Streamed straight from the committed .rodata catalog; no heap copy.
  req->send_P(200, "application/json",
              reinterpret_cast<const uint8_t*>(lamp::kExprCatalog),
              lamp::kExprCatalogLen);
}

static void handleGetNearby(AsyncWebServerRequest* req) {
  std::string json;
  ble_control::copyNearbyJson(json);
  req->send(200, "application/json", json.c_str());
}

// A map the heap could not build answers 503, never "{}": the editor PUTs the
// whole map back, so an empty one would erase every stored disposition.
static void handleGetDispositions(AsyncWebServerRequest* req) {
  bumpDeadline();
  try {
    const std::string json = s_config->asDispositionsJson();
    if (!json.empty()) {
      req->send(200, "application/json", json.c_str());
      return;
    }
  } catch (const std::bad_alloc&) {
  }
  req->send(503, "application/json", "{\"ok\":false}");
}

// Bulk replace of the per-peer disposition map, mirroring the auth-gated
// CHAR_SOCIAL_DISPOSITIONS BLE write: same Core-1 drain, same size guard,
// same OTA-quiet drop.
static void handlePutDispositions(AsyncWebServerRequest* req,
                                  JsonVariant& json) {
  bumpDeadline();
  if (webSurfaceReadOnly()) {
    req->send(403, "application/json", "{\"ok\":false}");
    return;
  }
  if (lamp::ota_quiet_mode::isQuiet()) {
    req->send(503, "application/json", "{\"ok\":false}");
    return;
  }
  String out;
  serializeJson(json, out);
  if (out.length() > lamp::kPendingJsonOp) {
    req->send(413, "application/json", "{\"ok\":false}");
    return;
  }
  postPendingSocialDispositionsJson(out.c_str(), out.length());
  req->send(200, "application/json", "{\"ok\":true}");
}

// A web save handed from the AsyncTCP task to the loop: the request body as
// it arrived, in one malloc'd block the loop frees, so the document is parsed
// once, on the loop (config_codec::saveRawJson). Each hand-off carries
// a sequence number and the loop publishes (seq, ok) for it, so a waiter only
// takes its own result. A waiter that times out withdraws its body if the
// loop has not taken it (nothing is saved); once taken, the save finishes and
// arms the reboot on its own.
struct WebSave {
  char* body = nullptr;
  uint32_t seq = 0;
};
static WebSave s_webSavePending;
static uint32_t s_webSaveSeq = 0;
static uint32_t s_webSaveDoneSeq = 0;
static lamp::config_codec::SaveResult s_webSaveDoneResult = lamp::config_codec::SaveResult::Ok;
static portMUX_TYPE s_webSaveMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_webSaveDone = nullptr;
static constexpr uint32_t kWebSaveWaitMs = 3000;
static constexpr uint32_t kRebootDelayMs = 1500;

enum class WebSaveResult { Saved, Failed, Invalid, Held, Withdrawn, StillSaving };

static WebSaveResult saveOnLoop(char* raw) {
  if (!s_webSaveDone) {
    free(raw);
    return WebSaveResult::Failed;
  }
  portENTER_CRITICAL(&s_webSaveMux);
  const uint32_t seq = ++s_webSaveSeq;
  char* stale = s_webSavePending.body;
  s_webSavePending = {raw, seq};
  portEXIT_CRITICAL(&s_webSaveMux);
  free(stale);

  const uint32_t deadline = millis() + kWebSaveWaitMs;
  while (true) {
    const int32_t left = static_cast<int32_t>(deadline - millis());
    if (left > 0 && xSemaphoreTake(s_webSaveDone, pdMS_TO_TICKS(left)) == pdTRUE) {
      portENTER_CRITICAL(&s_webSaveMux);
      const bool mine = s_webSaveDoneSeq == seq;
      const lamp::config_codec::SaveResult result = s_webSaveDoneResult;
      portEXIT_CRITICAL(&s_webSaveMux);
      if (mine) {
        switch (result) {
          case lamp::config_codec::SaveResult::Ok: return WebSaveResult::Saved;
          case lamp::config_codec::SaveResult::Invalid: return WebSaveResult::Invalid;
          case lamp::config_codec::SaveResult::Held: return WebSaveResult::Held;
          default: return WebSaveResult::Failed;
        }
      }
      continue;  // an earlier hand-off's result
    }
    portENTER_CRITICAL(&s_webSaveMux);
    char* withdrawn = nullptr;
    if (s_webSavePending.seq == seq) {
      withdrawn = s_webSavePending.body;
      s_webSavePending = {};
    }
    portEXIT_CRITICAL(&s_webSaveMux);
    if (withdrawn) {
      free(withdrawn);
      return WebSaveResult::Withdrawn;
    }
    return WebSaveResult::StillSaving;
  }
}

// Loop task: runs a pending web save and, when it lands, arms the reboot that
// loads it. Waits while a firmware update is being received, as every config
// save does; a handler that times out meanwhile withdraws its body.
static void drainWebSave() {
  if (firmwareReceiver.isInProgress()) return;
  portENTER_CRITICAL(&s_webSaveMux);
  const WebSave save = s_webSavePending;
  s_webSavePending = {};
  portEXIT_CRITICAL(&s_webSaveMux);
  if (!save.body) return;
  const lamp::config_codec::SaveResult result = s_config->persistRawJson(save.body);
  const bool ok = result == lamp::config_codec::SaveResult::Ok;
  free(save.body);
  // Every build: a handler that already answered 202 has no other way to
  // surface the failure.
  if (!ok) Serial.printf("[webapp] pending save failed (seq %lu)\n", (unsigned long)save.seq);
  if (ok) {
    s_fadeOnReboot = true;
    armReboot(millis() + kRebootDelayMs);
  }
  portENTER_CRITICAL(&s_webSaveMux);
  s_webSaveDoneSeq = save.seq;
  s_webSaveDoneResult = result;
  portEXIT_CRITICAL(&s_webSaveMux);
  xSemaphoreGive(s_webSaveDone);
}

static void collectSettingsBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                                size_t index, size_t total) {
  collectBodyPiece(req->_tempObject, data, len, index, total, kMaxSettingsBody,
                   lamp::heapCanSpare);
}

// Whole-document replace: the body must be the full config (the GET doc,
// mutated and sent back in its entirety). The loop parses it once
// (config_codec::saveRawJson) and refuses one with no `lamp` object or an
// over-long password or SSID; that answers 400 while this handler still
// waits, and is logged after a 202. A missing password keeps the stored one
// (GET redacts it), and a body carrying a name marks the lamp named.
static void handlePutSettings(AsyncWebServerRequest* req) {
  bumpDeadline();
  if (webSurfaceReadOnly()) {
    req->send(403, "application/json", "{\"ok\":false}");
    return;
  }
  const int status = settingsBodyStatus(req->contentType().startsWith("application/json"),
                                        req->contentLength(), req->_tempObject,
                                        kMaxSettingsBody);
  if (status != 0) {
    req->send(status, "application/json", "{\"ok\":false}");
    return;
  }
  char* body = takeBody(req->_tempObject);  // the loop frees it
  // A landed save arms its own reboot (drainWebSave): the constructor
  // re-parses the new config on boot, and drain-level side-effects in
  // apply::settingsBlobLocal() do not run inline for web saves. The 1.5 s
  // delay lets the HTTP response flush.
  switch (saveOnLoop(body)) {
    case WebSaveResult::Saved:
      lamp::logHeap("web-save");
      req->send(200, "application/json", "{\"ok\":true}");
      return;
    case WebSaveResult::StillSaving:
      // Taken by the loop but not done in time: it lands, and reboots, on
      // its own.
      req->send(202, "application/json", "{\"ok\":true,\"pending\":true}");
      return;
    case WebSaveResult::Invalid:
      req->send(400, "application/json", "{\"ok\":false}");
      return;
    case WebSaveResult::Held:
      // An earlier save waits for its reboot; this one would write over it.
      req->send(409, "application/json", "{\"ok\":false}");
      return;
    case WebSaveResult::Failed:
    case WebSaveResult::Withdrawn:
      req->send(500, "application/json", "{\"ok\":false}");
      return;
  }
}

// Server-verified password gate. Releases the stored plaintext only on a
// correct match so the UI can show it; GET keeps the password redacted.
static void handleUnlock(AsyncWebServerRequest* req, JsonVariant& json) {
  bumpDeadline();
  // Unthrottled and it returns the plaintext on a match, so on an open AP it
  // would be a free oracle for the BLE control password.
  if (webSurfaceReadOnly()) {
    req->send(403, "application/json", "{\"ok\":false}");
    return;
  }
  const std::string& stored = s_config->lamp.password;
  const std::string supplied = json["password"] | "";
  // Constant-time compare: accumulate diff over max(stored, supplied) length
  // so timing doesn't reveal the stored password length or a prefix match.
  const size_t maxLen = stored.size() > supplied.size() ? stored.size() : supplied.size();
  uint8_t diff = 0;
  for (size_t i = 0; i < maxLen; ++i) {
    const uint8_t a = i < stored.size()   ? static_cast<uint8_t>(stored[i])   : 0;
    const uint8_t b = i < supplied.size() ? static_cast<uint8_t>(supplied[i]) : 0;
    diff |= a ^ b;
  }
  if (stored.empty() || diff == 0) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["password"] = stored;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
    return;
  }
  req->send(401, "application/json", "{\"ok\":false}");
}

static void applyPreview(const JsonObject& obj) {
  if (obj["baseColors"].is<JsonArrayConst>()) {
    postColors(obj["baseColors"].as<JsonArrayConst>(), /*base=*/true);
  }
  if (obj["shadeColors"].is<JsonArrayConst>()) {
    postColors(obj["shadeColors"].as<JsonArrayConst>(), /*base=*/false);
  }
  if (obj["brightness"].is<int>()) {
    int level = obj["brightness"].as<int>();
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    postPendingBrightness(static_cast<int8_t>(level));
  }
}

static void onWsEvent(AsyncWebSocket* /*srv*/, AsyncWebSocketClient* /*client*/,
                      AwsEventType type, void* /*arg*/, uint8_t* data,
                      size_t len) {
  if (type == WS_EVT_CONNECT || type == WS_EVT_PONG) {
    bumpDeadline();
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    // A tab close / crash / wifi drop mid-edit never sends the closing
    // edit_session, so clear both surfaces or expressions stay paused forever.
    postPendingEditSession(0x03, false);
    return;
  }
  if (type != WS_EVT_DATA) return;
  bumpDeadline();
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  const char* action = doc["a"] | "";
  if (!strcmp(action, "test_expression") ||
      !strcmp(action, "test_expression_complete")) {
    // Same Core-0→Core-1 handoff discipline as the color path: drainTestAction
    // parses the full doc on the main loop, never the AsyncTCP task.
    postPendingTestActionJson(reinterpret_cast<const char*>(data), len);
    return;
  }
  if (!strcmp(action, "edit_session")) {
    // surface 1=base, 2=shade maps straight onto the operatorEditing
    // surface-mask bits the BLE CHAR_EDIT_SESSION path uses.
    const uint8_t surface = doc["surface"] | 0;
    postPendingEditSession(surface & 0x03, doc["open"] | false);
    return;
  }
  applyPreview(doc.as<JsonObject>());
}

static void onApStaConnected(arduino_event_id_t) { s_pendingServerStart = true; }
static void onApStaDisconnected(arduino_event_id_t) { s_pendingServerStop = true; }

// Stage 2: bring up the AsyncWebServer (spins the AsyncTCP task). Runs on the
// loop task, never the WiFi event task. Idempotent.
static void startServer() {
  if (s_serverUp) return;
  if (ble_control::deinitForWebapp()) {
    s_deadlineMs = millis() + LAMP_WEBAPP_IDLE_TIMEOUT_MS;
  }

  // Never auto-format: an FS-OTA'd image that mounts inconsistent must not be
  // silently wiped. A genuinely blank/corrupt partition just yields no UI
  // (BLE still works); production USB flash always lays down a valid image.
  // Coexists with fs_ota's own mount; SPIFFS.begin is a no-op when mounted.
  SPIFFS.begin(/*formatOnFail=*/false);

  buildSettingsSnapshot();

  s_server = new AsyncWebServer(kHttpPort);
  s_ws = new AsyncWebSocket("/ws");

  s_ws->onEvent(onWsEvent);
  s_server->addHandler(s_ws);

  s_server->on("/api/settings", HTTP_GET, handleGetSettings);
  s_server->on("/api/expressions", HTTP_GET, handleGetExpressions);
  s_server->on("/api/nearby", HTTP_GET, handleGetNearby);
  s_server->on("/api/dispositions", HTTP_GET, handleGetDispositions);

  s_server->addHandler(new AsyncCallbackJsonWebHandler("/api/dispositions",
                                                       handlePutDispositions));

  s_server->on("/api/settings", HTTP_PUT, handlePutSettings, nullptr, collectSettingsBody);

  s_server->addHandler(
      new AsyncCallbackJsonWebHandler("/api/unlock", handleUnlock));

  // serveStatic last so the explicit /api route + /ws win; it serves the
  // gzipped index.html.gz (Content-Encoding header auto-added) for "/" and
  // any other asset (e.g. the logo) straight from the SPIFFS image.
  s_server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  s_server->begin();
  if (MDNS.begin("lamp")) {
    MDNS.addService("http", "tcp", kHttpPort);
  }
  s_serverUp = true;

#ifdef LAMP_DEBUG
  Serial.printf("[webapp] server up at http://%s/ (lamp.local)\n",
                WiFi.softAPIP().toString().c_str());
#endif
}

// Stage 1: softAP only. The heavy server waits for a station to associate.
void begin(lamp::Config& config) {
  if (s_apUp) return;
  s_config = &config;
  if (!s_webSaveDone) s_webSaveDone = xSemaphoreCreateBinary();

  // SoftAP SSID is the lamp's name + "-lamp" so it's recognizable as a
  // lamp's setup network in a Wi-Fi scan.
  const std::string ssid = config.lamp.name + "-lamp";
  if (!wifi::startSoftAp(ssid, config.lamp.password)) {
#ifdef LAMP_DEBUG
    Serial.println("[webapp] softAP failed; webapp not started");
#endif
    return;
  }

  s_pendingServerStart = false;
  s_pendingServerStop = false;
  WiFi.onEvent(onApStaConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(onApStaDisconnected, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  const uint8_t bootMinutes = config.lamp.apBootMinutes;
  s_neverExpire = (bootMinutes == 0);
  s_deadlineMs = millis() + bootMinutes * 60000UL;
  s_rebootArmed = false;
  s_apUp = true;

#ifdef LAMP_DEBUG
  Serial.printf("[webapp] softAP up at %s  auth=%s bootWindow=%umin%s\n",
                WiFi.softAPIP().toString().c_str(),
                wifi::softApSecured() ? "wpa2" : "open", bootMinutes,
                s_neverExpire ? " (never expire)" : "");
#endif
}

// SoftAP-only teardown. The AsyncWebServer is never deleted at runtime.
// AsyncTCP ships no cooperative live-shutdown, so a DISCONNECTING station's
// in-flight lwip FIN/RST events reach the async task after a `delete s_server`
// and dereference freed memory (LoadProhibited). A resident server is freed
// only by a clean reboot.
static void stopAp() {
  if (!s_apUp) return;
  MDNS.end();
  wifi::stopSoftAp();
  s_apUp = false;
}

static void rebootNow() {
  if (s_fadeOnReboot) {
    // FadeOutBehavior draws over everything, fades to black over
    // REBOOT_ANIMATION_FRAMES (~1.9s), then calls ESP.restart() on its last
    // frame. Consume the flag and leave a fallback deadline: if this variant
    // has WebApp but not FadeOutBehavior (nothing consumes the flag), the hard
    // restart below still fires so the save always reboots.
    lamp::fadeOutRebootRequested = true;
    s_fadeOnReboot = false;
    armReboot(millis() + 4000);
    return;
  }
  delay(50);  // let the serial log flush before restart
  ESP.restart();
}

void tick() {
  drainWebSave();
  const uint32_t now = millis();
  uint32_t deadline = s_deadlineMs.load();
  const uint32_t held = holdPassedDeadline(now, deadline);
  if (held != deadline) s_deadlineMs.compare_exchange_strong(deadline, held);
  // An armed reboot fires even after the AP went down (an OTA's
  // shutdownForOta): a landed web save freezes config saves until it. It
  // waits while OTA quiet mode is held. A received update's own reboot loads
  // the save too; a failed one, or a distribution to other lamps (which never
  // reboots this one), leaves quiet mode and this reboot follows. Past
  // kRebootOtaWaitMaxMs the OTA is taken as stuck and the reboot goes ahead.
  const bool otaQuiet = lamp::ota_quiet_mode::isQuiet();
  if (webappRebootDue(now, s_rebootArmed, s_rebootAtMs, otaQuiet)) {
    if (otaQuiet) Serial.println("[webapp] reboot after 10 min OTA wait");
    rebootNow();  // save path; restart frees the server, never delete s_server
    return;
  }
  if (!s_apUp) return;

  // A BLE client's connect proves no station is associated (association takes
  // BLE down), so the heavy server cannot be up and stopAp() has no server to
  // race-free-drop. Reclaim the AP's heap for the BLE JSON sections.
  if (!s_serverUp && ble_control::isClientConnected()) {
    stopAp();
    return;
  }

  if (s_pendingServerStart.exchange(false)) startServer();
  if (s_pendingServerStop.exchange(false) && WiFi.softAPgetStationNum() == 0 &&
      ble_control::isDownForWebapp()) {
    armReboot(now);  // station left -> reboot restores BLE
  }
  if (!s_rebootArmed &&
      webappShouldRestoreBle(now, s_deadlineMs.load(), ble_control::isDownForWebapp())) {
    armReboot(now);  // idle (browser closed) -> reboot
  }

  // A connected station blocks expiry indefinitely; never reboot someone still
  // associated. A station mid-save (reboot armed) is likewise left alone.
  if (!s_rebootArmed && WiFi.softAPgetStationNum() == 0 &&
      webappShouldTeardown(now, s_deadlineMs.load(), s_neverExpire)) {
    if (s_serverUp) {
      rebootNow();  // reclaim the async task + server cleanly; delete races it
    } else {
      stopAp();  // nobody joined; drop the AP without a reboot
    }
  }
}

bool isActive() { return s_apUp; }

bool hasClient() { return s_apUp && WiFi.softAPgetStationNum() > 0; }

void shutdownForOta() {
  stopAp();
}

}  // namespace webapp

#endif  // LAMP_WEBAPP_ENABLED
