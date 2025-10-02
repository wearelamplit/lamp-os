#ifndef LAMP_CONFIG_CONFIG_H
#define LAMP_CONFIG_CONFIG_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "./config_types.hpp"

namespace lamp {
/**
 * @brief configurations file for the lamp that can be modified on the web
 * portal
 * @property lamp - global lamp details
 * @property base - details about the neopixels in the lamp base
 * @property shade - details about the neopixels in the lamp bulb
 */
class Config {
 private:
  Preferences* prefs;

 public:
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;

  Config() {};

  /**
   * @brief create a config based on information in the user's storage
   * @param [in] inPrefs preferences container for nvs values
   */
  Config(Preferences* inPrefs);

  /**
   * @brief create a streamable json doc to send configs to the webserver
   * @return a JsonDocument to serialize
   */
  JsonDocument asJsonDocument();
};
}  // namespace lamp

#endif