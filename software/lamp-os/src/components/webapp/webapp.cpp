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
#include <cstdio>
#include <string>

#include "components/network/wifi.hpp"
#include "config/config_types.hpp"
#include "util/color.hpp"

// Same Core 0 → Core 1 drain handoff the BLE writes use; mutating config
// directly from the AsyncTCP task would race the main loop.
void postPendingBaseColorsJson(const char* data, size_t len);
void postPendingShadeColorsJson(const char* data, size_t len);
void postPendingBrightness(int8_t level);

namespace webapp {

static lamp::Config* s_config = nullptr;
static AsyncWebServer* s_server = nullptr;
static AsyncWebSocket* s_ws = nullptr;

static uint32_t s_deadlineMs = 0;
static uint32_t s_rebootAtMs = 0;
static bool s_running = false;

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
// would race Core-1 BLE mutations; (2) we strip lamp.password here —
// asJsonDocument() emits it in plaintext and it must never reach a soft-AP
// client.
static String s_settingsJson;

static void buildSettingsSnapshot() {
  JsonDocument doc = s_config->asJsonDocument();
  doc["lamp"].remove("password");
  s_settingsJson = "";
  serializeJson(doc, s_settingsJson);
}

static void handleGetSettings(AsyncWebServerRequest* req) {
  bumpDeadline();
  req->send(200, "application/json", s_settingsJson);
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
  // 1.5s delay so the HTTP response flushes AND any lamp.name change picks up
  // on the BLE advert (set once at bluetooth.cpp:147). The constructor
  // re-parses the new blob on boot.
  s_rebootAtMs = millis() + 1500;
  req->send(200, "application/json", "{\"ok\":true}");
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
  if (type != WS_EVT_DATA) return;
  bumpDeadline();
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  applyPreview(doc.as<JsonObject>());
}

void begin(lamp::Config& config) {
  if (s_running) return;
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

  // Never auto-format: an FS-OTA'd image that mounts inconsistent must not be
  // silently wiped. A genuinely blank/corrupt partition just yields no UI
  // (BLE still works); production USB flash always lays down a valid image.
  SPIFFS.begin(/*formatOnFail=*/false);

  buildSettingsSnapshot();

  s_server = new AsyncWebServer(kHttpPort);
  s_ws = new AsyncWebSocket("/ws");

  s_ws->onEvent(onWsEvent);
  s_server->addHandler(s_ws);

  s_server->on("/api/settings", HTTP_GET, handleGetSettings);

  auto* putHandler =
      new AsyncCallbackJsonWebHandler("/api/settings", handlePutSettings);
  // Full config (expressions + dense knockout) can exceed the 16 KB default,
  // which AsyncJson drops silently. Give it generous headroom.
  putHandler->setMaxContentLength(32768);
  s_server->addHandler(putHandler);

  // serveStatic last so the explicit /api route + /ws win; it serves the
  // gzipped index.html.gz (Content-Encoding header auto-added) for "/" and
  // any other asset (e.g. the logo) straight from the SPIFFS image.
  s_server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  s_server->begin();
  if (MDNS.begin("lamp")) {
    MDNS.addService("http", "tcp", kHttpPort);
  }
  s_deadlineMs = millis() + LAMP_WEBAPP_BOOT_WINDOW_MS;
  s_rebootAtMs = 0;
  s_running = true;

#ifdef LAMP_DEBUG
  Serial.printf("[webapp] up at http://%s/ (lamp.local)  bootWindow=%dms\n",
                WiFi.softAPIP().toString().c_str(),
                LAMP_WEBAPP_BOOT_WINDOW_MS);
#endif
}

static void teardown() {
  if (!s_running) return;
  MDNS.end();
  if (s_server) {
    s_server->end();
    delete s_server;
    s_server = nullptr;
  }
  // AsyncWebSocket is freed by the server destructor (addHandler transfer).
  s_ws = nullptr;
  wifi::stopSoftAp();
  s_running = false;
  s_config = nullptr;
#ifdef LAMP_DEBUG
  Serial.println("[webapp] torn down");
#endif
}

void tick() {
  if (!s_running) return;
  const uint32_t now = millis();
  if (s_rebootAtMs != 0 && static_cast<int32_t>(now - s_rebootAtMs) >= 0) {
    teardown();
    delay(50);  // let the serial log flush before restart
    ESP.restart();
    return;  // unreachable
  }
  if (static_cast<int32_t>(now - s_deadlineMs) >= 0) {
    teardown();
  }
}

bool isActive() { return s_running; }

void shutdownForOta() {
  teardown();
}

}  // namespace webapp

#endif  // LAMP_WEBAPP_ENABLED
