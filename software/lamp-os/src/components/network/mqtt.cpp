#ifdef LAMP_MQTT_ENABLED

#include "./mqtt.hpp"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "./wifi.hpp"

#define MQTT_RECONNECT_INTERVAL_MS 5000
#define WIFI_RECONNECT_INTERVAL_MS 5000
#define MQTT_BUFFER_SIZE 512

namespace lamp {

// Static instance pointer for PubSubClient callback
static MqttComponent* mqttInstance = nullptr;

MqttComponent::MqttComponent() : mqttClient(wifiClient) {}

void MqttComponent::begin(Config* inConfig, WifiComponent* inWifi,
                           std::function<void(uint8_t)> onBrightnessChange,
                           std::function<void(bool)> onPowerChange) {
  config = inConfig;
  wifiComp = inWifi;
  brightnessCallback = onBrightnessChange;
  powerCallback = onPowerChange;
  mqttInstance = this;

  if (!config->mqtt.enabled || config->lamp.homeModeSSID.empty()) {
    return;
  }

  brightness = config->lamp.brightness;

  // Build lamp ID from name (sanitize for MQTT topic)
  lampId = config->lamp.name;

  // Build topic strings
  std::string prefix = config->mqtt.topicPrefix.empty() ? "homeassistant" : config->mqtt.topicPrefix;
  commandTopic = prefix + "/light/" + lampId + "/set";
  stateTopic = prefix + "/light/" + lampId + "/state";
  availabilityTopic = prefix + "/light/" + lampId + "/availability";
  discoveryTopic = "homeassistant/light/" + lampId + "/config";

  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setServer(config->mqtt.brokerHost.c_str(), config->mqtt.brokerPort);
  mqttClient.setCallback(messageCallback);

#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Initialized for lamp '%s', broker %s:%d\n",
                lampId.c_str(), config->mqtt.brokerHost.c_str(), config->mqtt.brokerPort);
#endif
}

void MqttComponent::tick(bool homeNetworkVisible) {
  if (!config || !config->mqtt.enabled || config->lamp.homeModeSSID.empty()) {
    return;
  }

  unsigned long now = millis();

  if (homeNetworkVisible && !staConnected) {
    // Home network detected but not connected — try to connect WiFi STA
    if (now - lastWifiAttemptMs > WIFI_RECONNECT_INTERVAL_MS) {
      lastWifiAttemptMs = now;
      connectWifi();
    }
  } else if (!homeNetworkVisible && staConnected) {
    // Home network gone — disconnect everything
    disconnectAll();
    return;
  }

  // Check if STA actually connected
  if (WiFi.isConnected()) {
    if (!staConnected) {
      staConnected = true;
      if (wifiComp) wifiComp->mqttStaActive = true;
#ifdef LAMP_DEBUG
      Serial.printf("[MQTT] WiFi STA connected, IP: %s\n", WiFi.localIP().toString().c_str());
#endif
    }

    // Manage MQTT connection
    if (!mqttClient.connected()) {
      if (now - lastReconnectAttemptMs > MQTT_RECONNECT_INTERVAL_MS) {
        lastReconnectAttemptMs = now;
        connectMqtt();
      }
    } else {
      mqttClient.loop();
    }
  } else if (staConnected) {
    // WiFi dropped unexpectedly
    staConnected = false;
#ifdef LAMP_DEBUG
    Serial.println("[MQTT] WiFi STA connection lost");
#endif
  }
}

void MqttComponent::connectWifi() {
#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Connecting to WiFi '%s'...\n", config->lamp.homeModeSSID.c_str());
#endif
  WiFi.begin(config->lamp.homeModeSSID.c_str(), config->lamp.homeModePassword.c_str());
}

void MqttComponent::disconnectAll() {
#ifdef LAMP_DEBUG
  Serial.println("[MQTT] Disconnecting (home network no longer visible)");
#endif
  if (mqttClient.connected()) {
    publishAvailability(false);
    mqttClient.disconnect();
  }
  WiFi.disconnect(false, false);  // Disconnect STA only, keep AP
  staConnected = false;
  if (wifiComp) wifiComp->mqttStaActive = false;
}

void MqttComponent::connectMqtt() {
  std::string clientId = "lamplit-" + lampId;

#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Connecting to broker %s:%d...\n",
                config->mqtt.brokerHost.c_str(), config->mqtt.brokerPort);
#endif

  // Set LWT (Last Will and Testament)
  bool connected;
  if (!config->mqtt.username.empty()) {
    connected = mqttClient.connect(
        clientId.c_str(),
        config->mqtt.username.c_str(),
        config->mqtt.password.c_str(),
        availabilityTopic.c_str(), 0, true, "offline");
  } else {
    connected = mqttClient.connect(
        clientId.c_str(),
        nullptr, nullptr,
        availabilityTopic.c_str(), 0, true, "offline");
  }

  if (connected) {
#ifdef LAMP_DEBUG
    Serial.println("[MQTT] Connected to broker");
#endif
    publishDiscovery();
    mqttClient.subscribe(commandTopic.c_str());
    publishAvailability(true);
    publishState();
  } else {
#ifdef LAMP_DEBUG
    Serial.printf("[MQTT] Connection failed, rc=%d\n", mqttClient.state());
#endif
  }
}

void MqttComponent::publishDiscovery() {
  JsonDocument doc;
  doc["name"] = config->lamp.name + " Lamp";
  doc["unique_id"] = lampId + "_light";
  doc["command_topic"] = commandTopic;
  doc["state_topic"] = stateTopic;
  doc["availability_topic"] = availabilityTopic;
  doc["schema"] = "json";
  doc["brightness"] = true;
  doc["brightness_scale"] = 100;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"].to<JsonArray>().add("lamplit_" + lampId);
  device["name"] = config->lamp.name + " Lamp";
  device["manufacturer"] = "Lamplit Art Society";
  device["model"] = "Standard Lamp";

  char buffer[MQTT_BUFFER_SIZE];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  mqttClient.publish(discoveryTopic.c_str(), buffer, true);

#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Published discovery (%d bytes) to %s\n", len, discoveryTopic.c_str());
#endif
}

void MqttComponent::publishAvailability(bool online) {
  mqttClient.publish(availabilityTopic.c_str(), online ? "online" : "offline", true);
}

void MqttComponent::publishState() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["state"] = powerState ? "ON" : "OFF";
  doc["brightness"] = brightness;

  char buffer[128];
  serializeJson(doc, buffer, sizeof(buffer));
  mqttClient.publish(stateTopic.c_str(), buffer, true);

#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Published state: %s\n", buffer);
#endif
}

void MqttComponent::messageCallback(char* topic, byte* payload, unsigned int length) {
  if (mqttInstance) {
    mqttInstance->handleMessage(reinterpret_cast<const char*>(payload), length);
  }
}

void MqttComponent::handleMessage(const char* payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
#ifdef LAMP_DEBUG
    Serial.printf("[MQTT] Failed to parse command: %s\n", error.c_str());
#endif
    return;
  }

#ifdef LAMP_DEBUG
  Serial.printf("[MQTT] Received command: %.*s\n", length, payload);
#endif

  if (doc["state"].is<const char*>()) {
    const char* state = doc["state"];
    bool on = (strcmp(state, "ON") == 0);
    powerState = on;
    if (powerCallback) {
      powerCallback(on);
    }
  }

  if (doc["brightness"].is<int>()) {
    brightness = doc["brightness"].as<uint8_t>();
    if (brightnessCallback) {
      brightnessCallback(brightness);
    }
  }

  publishState();
}

}  // namespace lamp

#endif  // LAMP_MQTT_ENABLED
