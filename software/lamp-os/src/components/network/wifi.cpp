#include "./wifi.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <Preferences.h>
#include <WiFi.h>

#include "../../behaviors/dmx.hpp"
#include "../../config/config.hpp"
#include "../../util/color.hpp"
#include "./artnet.hpp"
#include "SPIFFS.h"

namespace lamp {
ArtnetWifi artnet;
static AsyncWebServer server(80);
static AsyncWebSocketMessageHandler wsHandler;
static AsyncWebSocket ws("/ws", wsHandler.eventHandler());
static AsyncCorsMiddleware cors;
static DNSServer dnsServer;
Preferences prefs;

#ifdef LAMP_DEBUG
void wsMonitor() {
  wsHandler.onError([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       uint16_t errorCode, const char *reason, size_t len) {
    Serial.printf("Client %" PRIu32 " error: %" PRIu16 ": %s\n", client->id(),
                  errorCode, reason);
  });

  wsHandler.onFragment([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                          const AwsFrameInfo *frameInfo, const uint8_t *data,
                          size_t len) {
    Serial.printf("Client %" PRIu32 " fragment %" PRIu32 ": %s\n", client->id(),
                  frameInfo->num, (const char *)data);
  });
};
#endif

void onWiFiEvent(WiFiEvent_t event) {
#ifdef LAMP_DEBUG
  Serial.printf("WIFI Event %d\n", event);
#endif
};

// Intercepts only the well-known captive-portal-detection URLs each OS pings to
// decide whether the WiFi has internet. Returns a tiny self-contained landing
// page (no scripts, no external assets) so the captive popup WebView always
// renders it; clicking the button opens the full configurator at 192.168.4.1.
// Don't redirect to "/" here — the captive popup chokes on the Vue SPA (ES
// module + external Google Fonts that can't load through the captive network).
class CaptiveRequestHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    const String &u = request->url();
    return u == "/generate_204"             // Android
        || u == "/gen_204"                  // Android (older)
        || u == "/hotspot-detect.html"      // iOS / macOS
        || u == "/library/test/success.html"  // iOS
        || u == "/connecttest.txt"          // Windows
        || u == "/ncsi.txt"                 // Windows
        || u == "/redirect";                // Windows
  };

  void handleRequest(AsyncWebServerRequest *request) {
    static const char html[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Lamp connected</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px 24px;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:radial-gradient(circle at 50% 30%,#1E1B4B 0%,#000 70%);color:#fdfdfd;text-align:center;line-height:1.5;-webkit-font-smoothing:antialiased}
.critter{width:96px;height:96px;margin-bottom:28px;animation:float 4s ease-in-out infinite}
@keyframes float{0%,100%{transform:translateY(0)}50%{transform:translateY(-8px)}}
h1{font-size:26px;font-weight:700;margin-bottom:10px;letter-spacing:-0.01em}
p{color:#cccccc;max-width:320px;margin-bottom:28px;font-weight:300}
.btn{display:inline-block;padding:14px 32px;background:#C869C8;color:#fdfdfd;text-decoration:none;border-radius:8px;font-weight:600;font-size:16px;-webkit-tap-highlight-color:transparent}
.btn:active{background:#EFA8F0}
.hint{margin-top:28px;font-size:13px;color:#888}
.hint code{background:#1a1a1a;padding:3px 8px;border-radius:4px;color:#FFFDD1;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
</style>
</head>
<body>
<svg class="critter" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 62.33 77.31" fill="#FFFDD1" aria-hidden="true"><path d="M11.67,35C13.15,25.61,14.54,16.81,16,7.77c6.09-.48,12-.93,17-4.25,2.35,3,7.6,14.28,11.46,24.56a15.17,15.17,0,0,1-7.07,4.43A59,59,0,0,1,18,35.64C16.16,35.64,14.27,35.29,11.67,35Z"/><path d="M28.51,54.47a19.48,19.48,0,0,0-1.41,2.46c-1,2.56-2.78,3.55-5.52,3.65-4.88.19-8.55-1-8.48-8.17,0-3.65-.5-7.34.35-11,.25-1.05.67-2.05,1.07-3.26a61.85,61.85,0,0,0,28.33-5.06A12.39,12.39,0,0,1,44,35.76,32.87,32.87,0,0,1,43.4,50a15.83,15.83,0,0,1-1.63,3.94c-3,5-7.64,5.49-11.91,1.48A10.91,10.91,0,0,0,28.51,54.47Z"/><path d="M31.9,63.4A13.2,13.2,0,0,0,39.8,61c3.34,3.72,3.62,8,3.09,12.33-.45,3.63-4.07,5.11-7.23,3.18a20.74,20.74,0,0,1-2.68-2A2.9,2.9,0,0,1,31.87,73C31.49,69.85,30.22,66.72,31.9,63.4Z"/><path d="M14.74,63.07H25.49a17.86,17.86,0,0,1,.24,9.55,3.83,3.83,0,0,1-3.46,2.87,9.89,9.89,0,0,1-6.48-1.09c-3.24-1.83-4.13-4.08-2.86-7.59A39.26,39.26,0,0,1,14.74,63.07Z"/><path d="M9.25,31.71c-.51-.11-.91-.07-1.08-.25-2-2.21-4.09-4.42-6-6.71a9.58,9.58,0,0,1-2-8,2.68,2.68,0,0,1,2-2.41,8.22,8.22,0,0,1,8.87,4,2.69,2.69,0,0,1,.27,1.38A69.56,69.56,0,0,1,9.25,31.71Z"/><path d="M45.63,22.73a74.26,74.26,0,0,1-5-12.06c-.86-3,2.25-7.52,5.26-8A3.2,3.2,0,0,1,49.4,5.23a15.56,15.56,0,0,1,.1,7.52,7.47,7.47,0,0,1-.45,1.36C48,16.87,46.87,19.62,45.63,22.73Z"/><path d="M30.82,1c-.49,1.66-2.07,1.79-3.32,2.21-3,1-6,2-9.24,1.23C21.07-.21,25-.92,30.82,1Z"/><path d="M62.33,22.24c-.53.22-1,.61-1.29.51a19.5,19.5,0,0,1-7.22-4.41c-.53-.52-.3-1.1.77-2.16,1.34,1,2.68,2.07,4,3.08S61.56,20.47,62.33,22.24Z"/><path d="M59.37,27.43c-1,0-1.52.17-1.81,0C55.51,26,53.49,24.49,51.51,23c-.77-.59-.69-1.29,0-1.93C55.92,23,56.84,23.67,59.37,27.43Z"/></svg>
<h1>Lamp WiFi connected</h1>
<p>Open your browser to configure this lamp.</p>
<a class="btn" href="http://192.168.4.1/">Open Configurator</a>
<p class="hint">Or visit <code>192.168.4.1</code> in any browser</p>
</body>
</html>)HTML";
    request->send_P(200, "text/html", html);
  };
};

WifiComponent::WifiComponent() {};

void WifiComponent::begin(Config *inConfig) {
#ifdef LAMP_DEBUG
  Serial.printf("Starting Wifi Async Client\n");
#endif
  Serial.begin(115200);
  config = inConfig;
  serializeJson(config->asJsonDocument(), doc);
  WiFi.setSleep(false);
  WiFi.onEvent(onWiFiEvent);
  toApMode();

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  MDNS.begin("lamp");
#ifdef LAMP_DEBUG
  wsMonitor();
#endif
  wsHandler.onMessage([&](AsyncWebSocket *server, AsyncWebSocketClient *client, const uint8_t *data, size_t len) {
#ifdef LAMP_DEBUG
    Serial.printf("Client %" PRIu32 " data: %s\n", client->id(), (const char *)data);
#endif
    lastWebSocketUpdateTimeMs = millis();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data);

    if (error) {
#ifdef LAMP_DEBUG
      Serial.printf("ws deserializeJson() failed: %s\n", error.c_str());
#endif
      return;  // use class defaults
    }

    newWebSocketData = true;
    lastWebSocketData = doc;
  });
  wsHandler.onConnect([&](AsyncWebSocket *server, AsyncWebSocketClient *client) {
#ifdef LAMP_DEBUG
    Serial.printf("Client %" PRIu32 " connected\n", client->id());
#endif
    lastWebSocketUpdateTimeMs = millis();
  });
  wsHandler.onDisconnect([&](AsyncWebSocket *server, uint32_t clientId) {
#ifdef LAMP_DEBUG
    Serial.printf("Client %" PRIu32 " disconnected\n", clientId);
#endif
    lastWebSocketUpdateTimeMs = millis();
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html.gz", "text/html");
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });
  server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print(doc.c_str());
    request->send(response);
  });
  server.on(
      "/settings",
      HTTP_PUT,
      [](AsyncWebServerRequest *request) {},
      nullptr,
      [&](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        size_t status = 0;
        try {
          String buf;
          for (size_t i = 0; i < len; i++) {
            buf.concat((char)data[i]);
          }
          prefs.begin("lamp", false);
          status = prefs.putString("cfg", buf);
          prefs.end();

          if (status) {
            requiresReboot = true;
            request->send(200);
            return;
          }
        } catch (int e) {
#ifdef LAMP_DEBUG
          Serial.printf("Setting threw with status %d - e: %d\n", status, e);
#endif
          request->send(500);
          return;
        }

#ifdef LAMP_DEBUG
        Serial.printf("Setting failed with status %d", status);
#endif
        request->send(500);
        return;
      });

  cors.setMethods("POST, PUT, GET, OPTIONS, DELETE");
  ElegantOTA.begin(&server);
  ElegantOTA.onEnd([this](bool success) {
    if (success) {
      requiresReboot = true;
    }
  });
  server.addMiddleware(&cors);
  server.addHandler(&ws);
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);
  server.begin();
  dnsServer.start(53, "*", WiFi.softAPIP());
  artnet.begin();
};

void WifiComponent::tick() {
  uint32_t now = millis();

  dnsServer.processNextRequest();

  if (now > lastWebSocketCleanTimeMs + WEBSOCKET_CLEAN_TIME_MS &&
      now > lastWebSocketUpdateTimeMs + WEBSOCKET_CLEAN_TIME_MS) {
    ws.cleanupClients(1);
    lastWebSocketCleanTimeMs = now;
#ifdef LAMP_DEBUG
    Serial.printf("WS free heap: %" PRIu32 "\n", ESP.getFreeHeap());
#endif
  }

  // Update network scan every 30 seconds if home mode SSID is configured
  // This mode has side effects on both Station and AP modes and will interrupt
  // their connectivity. Check that:
  // - The user has their lamp in home SSID scanning mode
  // - The user is not using the web configuration tool at the moment
  // - The lamp isn't actively receiving recent artnet packets
  if (!config->lamp.homeModeSSID.empty() &&
      !scanInProgress &&
      ws.count() == 0 &&
      (now < 5 || now > getLastArtnetFrameTimeMs() + DMX_ARTNET_TIMEOUT_MS - 1) &&
      now > lastNetworkScanTimeMs + 30000) {
    WiFi.scanNetworks(true);
    scanInProgress = true;
    lastNetworkScanTimeMs = now;
  }

  if (scanInProgress) {
    int16_t result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      homeNetworkVisible = false;
      for (int16_t i = 0; i < result; ++i) {
        if (WiFi.SSID(i).equalsIgnoreCase(config->lamp.homeModeSSID.c_str())) {
          homeNetworkVisible = true;
          break;
        }
      }
      WiFi.scanDelete();
      scanInProgress = false;
    }
  }
};

ArtnetDetail WifiComponent::getArtnetData() {
  return artnet.artnetData;
};

unsigned long WifiComponent::getLastArtnetFrameTimeMs() {
  return artnet.lastDmxFrameMs;
};

bool WifiComponent::hasWebSocketData() { return newWebSocketData; };

unsigned long WifiComponent::getLastWebSocketUpdateTimeMs() {
  return lastWebSocketUpdateTimeMs;
};

JsonDocument WifiComponent::getWebSocketData() {
  newWebSocketData = false;
  return lastWebSocketData;
};

void WifiComponent::toStageMode(String inSsid, String inPassword) {
  stageMode = true;
  WiFi.begin(inSsid, inPassword, WIFI_PREFERRED_CHANNEL);
  WiFi.setAutoReconnect(true);
};

void WifiComponent::toApMode() {
  stageMode = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(
      config->lamp.name.substr(0, 12).append("-lamp").c_str(),
      String(config->lamp.password.c_str()),
      WIFI_PREFERRED_CHANNEL);
};

bool WifiComponent::isHomeNetworkVisible() {
  return homeNetworkVisible;
};
}  // namespace lamp