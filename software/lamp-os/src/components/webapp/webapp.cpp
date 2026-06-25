#if LAMP_WEBAPP_ENABLED

#include "webapp.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "components/network/wifi.hpp"
#include "config/config_types.hpp"
#include "index_html_gz.h"
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

static constexpr size_t kMaxColors = 5;

static std::string colorAsHexww(const lamp::Color& c) {
  return lamp::colorToHexString(c);
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

static void writeSettingsJson(AsyncResponseStream* res) {
  JsonDocument doc;
  doc["name"] = s_config->lamp.name;
  JsonArray bArr = doc["baseColors"].to<JsonArray>();
  for (const auto& c : s_config->base.colors) bArr.add(colorAsHexww(c));
  JsonArray sArr = doc["shadeColors"].to<JsonArray>();
  for (const auto& c : s_config->shade.colors) sArr.add(colorAsHexww(c));
  doc["brightness"] = s_config->lamp.brightness;
  serializeJson(doc, *res);
}

static void absorbColors(JsonArrayConst arr, std::vector<lamp::Color>& dst,
                         bool& changedOut) {
  if (arr.isNull() || arr.size() == 0) return;
  const size_t n = std::min<size_t>(arr.size(), kMaxColors);
  std::vector<lamp::Color> next;
  next.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const char* h = arr[i].as<const char*>();
    if (!h) return;  // bad payload — leave dst untouched
    next.push_back(lamp::hexStringToColor(h));
  }
  if (next != dst) {
    dst = std::move(next);
    changedOut = true;
  }
}

static void handleGetSettings(AsyncWebServerRequest* req) {
  bumpDeadline();
  auto* res = req->beginResponseStream("application/json");
  writeSettingsJson(res);
  req->send(res);
}

static void handlePutSettings(AsyncWebServerRequest* req, JsonVariant& json) {
  bumpDeadline();
  JsonObject obj = json.as<JsonObject>();
  bool changed = false;
  if (obj["name"].is<const char*>()) {
    std::string name = obj["name"].as<const char*>();
    // 12-char ceiling matches NimBLE's truncation at bluetooth.cpp:147 —
    // clamp here so NVS and the BLE advertised name stay in sync.
    if (name.size() > 12) name.resize(12);
    if (!name.empty() && name != s_config->lamp.name) {
      s_config->lamp.name = name;
      s_config->invalidateLampSection();
      changed = true;
    }
  }
  bool baseChanged = false;
  bool shadeChanged = false;
  if (obj["baseColors"].is<JsonArrayConst>()) {
    absorbColors(obj["baseColors"].as<JsonArrayConst>(), s_config->base.colors,
                 baseChanged);
  }
  if (obj["shadeColors"].is<JsonArrayConst>()) {
    absorbColors(obj["shadeColors"].as<JsonArrayConst>(),
                 s_config->shade.colors, shadeChanged);
  }
  if (baseChanged) {
    if (s_config->base.ac >= s_config->base.colors.size()) {
      s_config->base.ac = 0;
    }
    s_config->invalidateBaseSection();
    changed = true;
  }
  if (shadeChanged) {
    s_config->invalidateShadeSection();
    changed = true;
  }
  if (obj["brightness"].is<int>()) {
    int level = obj["brightness"].as<int>();
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (s_config->lamp.brightness != static_cast<uint8_t>(level)) {
      s_config->lamp.brightness = static_cast<uint8_t>(level);
      s_config->invalidateLampSection();
      changed = true;
    }
  }
  if (changed && !s_config->persistConfig("webapp")) {
    req->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  // 1.5s delay so the HTTP response flushes AND any lamp.name change
  // picks up on the BLE advert (set once at bluetooth.cpp:147).
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

static void handleIndex(AsyncWebServerRequest* req) {
  bumpDeadline();
  auto* res = req->beginResponse_P(
      200, "text/html", reinterpret_cast<const uint8_t*>(kIndexHtml),
      kIndexHtmlLen);
  if (kIndexHtmlGzipped) res->addHeader("Content-Encoding", "gzip");
  req->send(res);
}

void begin(lamp::Config& config) {
  if (s_running) return;
  s_config = &config;

  // Don't double-suffix when the configured name already ends in "-lamp"
  // (the unconfigured "anonymous-lamp" default would otherwise yield
  // "anonymous-lamp-lamp").
  std::string ssid = config.lamp.name;
  const std::string suffix = "-lamp";
  if (ssid.size() < suffix.size() ||
      ssid.compare(ssid.size() - suffix.size(), suffix.size(), suffix) != 0) {
    ssid += suffix;
  }
  if (!wifi::startSoftAp(ssid)) {
#ifdef LAMP_DEBUG
    Serial.println("[webapp] softAP failed; webapp not started");
#endif
    return;
  }

  s_server = new AsyncWebServer(kHttpPort);
  s_ws = new AsyncWebSocket("/ws");

  s_ws->onEvent(onWsEvent);
  s_server->addHandler(s_ws);

  s_server->on("/", HTTP_GET, handleIndex);
  s_server->on("/api/settings", HTTP_GET, handleGetSettings);

  auto* putHandler =
      new AsyncCallbackJsonWebHandler("/api/settings", handlePutSettings);
  s_server->addHandler(putHandler);

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
