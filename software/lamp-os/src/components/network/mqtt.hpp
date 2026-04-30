#ifndef LAMP_COMPONENTS_NETWORK_MQTT_H
#define LAMP_COMPONENTS_NETWORK_MQTT_H

#ifdef LAMP_MQTT_ENABLED

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <functional>
#include <string>

#include "../../config/config.hpp"

namespace lamp {

class MqttComponent {
 public:
  MqttComponent();

  /**
   * @brief Initialize MQTT with config and callbacks for power/brightness control
   * @param inConfig lamp configuration reference
   * @param onBrightnessChange called when HA sets brightness (0-100)
   * @param onPowerChange called when HA toggles power
   */
  void begin(Config* inConfig, std::function<void(uint8_t)> onBrightnessChange,
             std::function<void(bool)> onPowerChange);

  /**
   * @brief Called every loop iteration. Manages WiFi STA and MQTT connections
   *        based on home network visibility.
   * @param homeNetworkVisible true if the home SSID was detected in scan
   */
  void tick(bool homeNetworkVisible);

  /**
   * @brief Publish current power/brightness state to HA.
   *        Call this when state changes from the config UI.
   */
  void publishState();

 private:
  Config* config = nullptr;
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  bool staConnected = false;
  bool powerState = true;
  uint8_t brightness = 100;
  unsigned long lastReconnectAttemptMs = 0;
  unsigned long lastWifiAttemptMs = 0;
  std::string lampId;
  std::string commandTopic;
  std::string stateTopic;
  std::string availabilityTopic;
  std::string discoveryTopic;
  std::function<void(uint8_t)> brightnessCallback;
  std::function<void(bool)> powerCallback;

  void connectWifi();
  void disconnectAll();
  void connectMqtt();
  void publishDiscovery();
  void publishAvailability(bool online);
  static void messageCallback(char* topic, byte* payload, unsigned int length);
  void handleMessage(const char* payload, unsigned int length);
};

}  // namespace lamp

#endif  // LAMP_MQTT_ENABLED
#endif  // LAMP_COMPONENTS_NETWORK_MQTT_H
