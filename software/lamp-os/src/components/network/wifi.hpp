#ifndef LAMP_COMPONENTS_NETWORK_WIFI_H
#define LAMP_COMPONENTS_NETWORK_WIFI_H

#include <Arduino.h>

#include <string>
#include <vector>

#include "../../config/config.hpp"
#include "../../util/color.hpp"
#include "./artnet.hpp"

#define WEBSOCKET_CLEAN_TIME_MS 60000
#define WIFI_PREFERRED_CHANNEL 6

namespace lamp {

class WifiComponent {
 public:
  Config* config;
  unsigned long lastWebSocketCleanTimeMs = 0;
  std::string doc;
  bool requiresReboot = false;
  bool newWebSocketData = false;
  bool stageMode = false;
  unsigned long lastWebSocketUpdateTimeMs = 0;
  JsonDocument lastWebSocketData;
  bool homeNetworkVisible = false;
  unsigned long lastNetworkScanTimeMs = 0;

  WifiComponent();

  /**
   * @brief initializer for setup
   * @param [in] config a reference to a lamp config
   */
  void begin(Config* inConfig);

  /**
   * @brief process to call in the main loop
   */
  void tick();

  /**
   * @brief get last Artnet UDP data from the buffer
   * @return details of the 10 channels occupied by a lamp
   */
  ArtnetDetail getArtnetData();

  /**
   * @brief get last Artnet UDP frame time
   * @return timestamp of last frame in milliseconds
   */
  unsigned long getLastArtnetFrameTimeMs();

  /**
   * @brief Check if there's new WebSocket data to consume
   * @return true if there's recent data in the buffer
   */
  bool hasWebSocketData();

  /**
   * @brief get last WebSocket updates time
   * @return timestamp of last update time in milliseconds
   */
  unsigned long getLastWebSocketUpdateTimeMs();

  /**
   * @brief get the json doc from the buffer
   * @return JsonDocument
   */
  JsonDocument getWebSocketData();

  /**
   * @brief enable the station mode for receiving ArtNet data
   * @param [in] inSsid the ssid advertised by the stage
   * @param [in] inPassword the password advertised by the stage
   */
  void toStageMode(String inSsid, String inPassword);

  /**
   * @brief if the stage is no longer found, disconnect from the STA to restore the SoftAP
   */
  void toApMode();

  /**
   * @brief Check if the configured home network SSID is visible
   * @return true if home network SSID is detected in scan results
   */
  bool isHomeNetworkVisible();

  /**
   * @brief Update network scan results to check for home SSID
   */
  void updateNetworkScan();
};
}  // namespace lamp
#endif