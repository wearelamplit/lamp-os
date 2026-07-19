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
#include <esp_system.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "components/firmware/ota_quiet_mode.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/transport/wifi.hpp"
#include "components/webapp/webapp_deadline.hpp"
#include "config/config_types.hpp"
#include "core/pending_json_slot.hpp"
#include "expressions/expression_manager.hpp"
#include "util/color.hpp"
#include "util/heap_probe.hpp"

// Same Core 0 → Core 1 drain handoff the BLE writes use; mutating config
// directly from the AsyncTCP task would race the main loop.
void postPendingBaseColorsJson(const char* data, size_t len);
void postPendingShadeColorsJson(const char* data, size_t len);
void postPendingBrightness(int8_t level);
void postPendingEditSession(uint8_t surfaceMask, bool open);
void postPendingTestActionJson(const char* data, size_t len);
void postPendingSocialDispositionsJson(const char* data, size_t len);

namespace webapp {

static lamp::Config* s_config = nullptr;
static AsyncWebServer* s_server = nullptr;
static AsyncWebSocket* s_ws = nullptr;

static uint32_t s_deadlineMs = 0;
static uint32_t s_rebootAtMs = 0;
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
  if (idle > s_deadlineMs) s_deadlineMs = idle;
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
  doc["lamp"]["lampId"] = s_config->lampId();
  doc["lamp"].remove("password");
  s_settingsJson = "";
  serializeJson(doc, s_settingsJson);
}

static void handleGetSettings(AsyncWebServerRequest* req) {
  bumpDeadline();
  req->send(200, "application/json", s_settingsJson);
}

static void handleGetExpressions(AsyncWebServerRequest* req) {
  req->send(200, "application/json", lamp::expressionCatalogJson().c_str());
}

static void handleGetNearby(AsyncWebServerRequest* req) {
  std::string json;
  ble_control::copyNearbyJson(json);
  req->send(200, "application/json", json.c_str());
}

static void handleGetDispositions(AsyncWebServerRequest* req) {
  bumpDeadline();
  req->send(200, "application/json", s_config->asDispositionsJson());
}

// Bulk replace of the per-peer disposition map, mirroring the auth-gated
// CHAR_SOCIAL_DISPOSITIONS BLE write: same Core-1 drain, same size guard,
// same OTA-quiet drop.
static void handlePutDispositions(AsyncWebServerRequest* req,
                                  JsonVariant& json) {
  bumpDeadline();
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

static void handlePutSettings(AsyncWebServerRequest* req, JsonVariant& json) {
  bumpDeadline();
  JsonObject obj = json.as<JsonObject>();
  // Whole-document replace: the body must be the full config (the GET doc,
  // mutated and sent back in its entirety). Reject anything without a `lamp`
  // object so a partial/garbage body can't wipe config back to constructor
  // defaults on the reboot below.
  if (obj.isNull() || !obj["lamp"].is<JsonObject>()) {
    req->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  // GET redacts the password, so the body won't carry it. Re-inject the
  // server-side value (the from-scratch reload on boot only keeps a password
  // present in the blob, so omitting it would wipe the control password).
  const char* pw = obj["lamp"]["password"] | "";
  if (pw[0] == '\0' && !s_config->lamp.password.empty()) {
    obj["lamp"]["password"] = s_config->lamp.password;
  }
  String out;
  serializeJson(json, out);
  if (!s_config->persistRawJson(out.c_str())) {
    req->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  lamp::logHeap("web-save");
  // Reboot re-applies settings via the constructor; drain-level side-effects
  // in apply::settingsBlobLocal() do not run inline for web saves.
  // 1.5s delay so the HTTP response flushes AND any lamp.name change picks up
  // on the BLE advert (set once at bluetooth.cpp:147). The constructor
  // re-parses the new blob on boot.
  s_rebootAtMs = millis() + 1500;
  req->send(200, "application/json", "{\"ok\":true}");
}

// Server-verified password gate. Releases the stored plaintext only on a
// correct match so the UI can show it; GET keeps the password redacted.
static void handleUnlock(AsyncWebServerRequest* req, JsonVariant& json) {
  bumpDeadline();
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

  auto* putHandler =
      new AsyncCallbackJsonWebHandler("/api/settings", handlePutSettings);
  // Full config (expressions + dense knockout) can exceed the 16 KB default,
  // which AsyncJson drops silently. Give it generous headroom.
  putHandler->setMaxContentLength(32768);
  s_server->addHandler(putHandler);

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

  // SoftAP SSID is the lamp's name + "-lamp" so it's recognizable as a
  // lamp's setup network in a Wi-Fi scan.
  const std::string ssid = config.lamp.name + "-lamp";
  if (!wifi::startSoftAp(ssid)) {
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
  s_rebootAtMs = 0;
  s_apUp = true;

#ifdef LAMP_DEBUG
  Serial.printf("[webapp] softAP up at %s  bootWindow=%umin%s\n",
                WiFi.softAPIP().toString().c_str(), bootMinutes,
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
  delay(50);  // let the serial log flush before restart
  ESP.restart();
}

void tick() {
  if (!s_apUp) return;
  const uint32_t now = millis();

  if (s_pendingServerStart.exchange(false)) startServer();
  if (s_pendingServerStop.exchange(false) && WiFi.softAPgetStationNum() == 0) {
    bumpDeadline();  // last station left; grace before reboot, survives a rejoin
  }

  if (s_rebootAtMs != 0 && static_cast<int32_t>(now - s_rebootAtMs) >= 0) {
    rebootNow();  // save path; restart frees the server, never delete s_server
    return;
  }

  // A connected station blocks expiry indefinitely; never reboot someone still
  // associated. A station mid-save (reboot armed) is likewise left alone.
  if (s_rebootAtMs == 0 && WiFi.softAPgetStationNum() == 0 &&
      webappShouldTeardown(now, s_deadlineMs, s_neverExpire)) {
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
