#include "./wifi.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
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

class CaptiveRequestHandler : public AsyncWebHandler {
 public:
  bool canHandle(__unused AsyncWebServerRequest *request) const override {
    return true;
  };

  void handleRequest(AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html.gz", String(), false);
  };
};

WifiComponent::WifiComponent() {};

void WifiComponent::begin(Config *inConfig) {
#ifdef LAMP_DEBUG
  Serial.printf("Starting Wifi Async Client\n");
#endif
  Serial.begin(115200);
  config = inConfig;

  // Build the settings JSON for the GET /settings API response,
  // stripping sensitive fields that should not be exposed
  JsonDocument settingsDoc = config->asJsonDocument();
  if (settingsDoc["lamp"]["homeModePassword"].is<const char*>()) {
    settingsDoc["lamp"]["homeModePassword"] = "********";
  }
#ifdef LAMP_MQTT_ENABLED
  if (settingsDoc["mqtt"]["password"].is<const char*>()) {
    settingsDoc["mqtt"]["password"] = "********";
  }
#endif
  serializeJson(settingsDoc, doc);
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

          // Restore masked passwords with their original values before saving
          JsonDocument incoming;
          if (!deserializeJson(incoming, buf)) {
            String masked = "********";
            if (incoming["lamp"]["homeModePassword"].as<String>() == masked) {
              incoming["lamp"]["homeModePassword"] = config->lamp.homeModePassword;
            }
#ifdef LAMP_MQTT_ENABLED
            if (incoming["mqtt"]["password"].as<String>() == masked) {
              incoming["mqtt"]["password"] = config->mqtt.password;
            }
#endif
            buf = "";
            serializeJson(incoming, buf);
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
  server.begin();
  artnet.begin();
};

void WifiComponent::tick() {
  uint32_t now = millis();

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
      !mqttStaActive &&
      ws.count() == 0 &&
      (now < 5 || now > getLastArtnetFrameTimeMs() + DMX_ARTNET_TIMEOUT_MS - 1) &&
      now > lastNetworkScanTimeMs + 30000) {
    updateNetworkScan();
    lastNetworkScanTimeMs = now;
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

void WifiComponent::updateNetworkScan() {
  if (config->lamp.homeModeSSID.empty()) {
    homeNetworkVisible = false;
    return;
  }

  homeNetworkVisible = false;

  int n = WiFi.scanNetworks();
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.equals(config->lamp.homeModeSSID.c_str())) {
        homeNetworkVisible = true;
        break;
      }
    }
  }
  WiFi.scanDelete();
};
}  // namespace lamp